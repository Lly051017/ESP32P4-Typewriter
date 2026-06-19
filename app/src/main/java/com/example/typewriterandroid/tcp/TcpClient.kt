package com.example.typewriterandroid.tcp

import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlin.coroutines.coroutineContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.IOException
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.InetSocketAddress
import java.net.Socket

/**
 * TCP 客户端 —— 封装 Socket 通信 + 延时指令队列 + 异步接收。
 *
 * ## 架构
 *  - StateFlow：驱动 UI 自动刷新（替代 Qt Q_PROPERTY NOTIFY）
 *  - SharedFlow：事件通知（替代 Qt signal）
 *  - 协程 + delay()：延时发送队列（替代 Qt QTimer::singleShot 递归）
 *  - Dispatchers.IO：后台线程，不阻塞 UI
 *
 * ## 线程安全
 *  - `queue` 被 `sendCommand()`（任意线程）和 `processQueue()`（IO 协程）
 *    并发访问，所有队列操作在 `synchronized(queue) {}` 内执行。
 *
 * ## 用法（ViewModel 层）
 *   viewModelScope.launch {
 *       tcpClient.connect("192.168.1.100", 8080)
 *   }
 *   tcpClient.sendCommand("G21")
 *   tcpClient.sendCommands(listOf("G90", "G0 X0 Y0"))
 *   tcpClient.disconnect()
 */
class TcpClient {

    // =========================================================================
    // 暴露给 UI 的状态流
    // =========================================================================

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _pendingCount = MutableStateFlow(0)
    val pendingCount: StateFlow<Int> = _pendingCount.asStateFlow()

    private val _commandSent = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val commandSent: SharedFlow<String> = _commandSent.asSharedFlow()

    private val _commandAck = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val commandAck: SharedFlow<String> = _commandAck.asSharedFlow()

    // =========================================================================
    // 内部状态
    // =========================================================================

    private var socket: Socket? = null
    private var writer: BufferedWriter? = null
    private var reader: BufferedReader? = null
    private var scope: CoroutineScope? = null
    private val queue = ArrayDeque<String>()

    // =========================================================================
    // 连接 / 断开
    // =========================================================================

    /**
     * 发起 TCP 连接（挂起函数，在 IO 线程调用）。
     *
     * @param host  ESP32 IP 地址
     * @param port  端口，默认 8080
     * @throws IOException 连接失败时抛出
     */
    suspend fun connect(host: String, port: Int = 8080) {
        _connectionState.value = ConnectionState.Connecting

        try {
            val newSocket = Socket()
            newSocket.connect(InetSocketAddress(host, port), 5000) // 5 秒超时
            newSocket.tcpNoDelay = true   // 禁用 Nagle 算法，每条指令立即发出

            socket = newSocket
            writer = BufferedWriter(OutputStreamWriter(
                newSocket.getOutputStream(), Charsets.UTF_8))
            reader = BufferedReader(InputStreamReader(
                newSocket.getInputStream(), Charsets.UTF_8))

            _connectionState.value = ConnectionState.Connected

            // 启动后台协程
            scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
            scope?.launch { receiveLoop() }
            scope?.launch { processQueue() }

        } catch (e: Exception) {
            _connectionState.value = ConnectionState.Error(
                e.message ?: "连接失败")
            throw e
        }
    }

    /** 断开连接，取消所有后台任务，清空队列。 */
    fun disconnect() {
        scope?.cancel()
        scope = null

        try { socket?.close() } catch (_: Exception) {}
        socket = null
        writer = null
        reader = null

        synchronized(queue) {
            queue.clear()
        }
        _pendingCount.value = 0
        _connectionState.value = ConnectionState.Disconnected
    }

    // =========================================================================
    // 发送接口（非阻塞，调用后可立即返回）
    // =========================================================================

    /** 单条指令入队。 */
    fun sendCommand(cmd: String) {
        synchronized(queue) {
            queue.addLast(cmd)
            _pendingCount.value = queue.size
        }
    }

    /** 批量指令入队。 */
    fun sendCommands(cmds: List<String>) {
        synchronized(queue) {
            queue.addAll(cmds)
            _pendingCount.value = queue.size
        }
    }

    /** 清空待发送队列（紧急停止）。 */
    fun clearQueue() {
        synchronized(queue) {
            queue.clear()
        }
        _pendingCount.value = 0
    }

    // =========================================================================
    // 核心：延时发送队列（协程驱动）
    // =========================================================================

    /**
     * 从队列逐条取出指令，发送后根据指令类型延时，再取下一跳。
     *
     * 等价于 Qt 版的：
     *   QTimer::singleShot(delay, this, &TcpClient::processQueue)
     * 但这里用 for 循环 + delay() 一气呵成。
     */
    private suspend fun processQueue() {
        while (coroutineContext.isActive) {
            // synchronized 返回待发送的指令，null 表示队列空
            // delay() 在块外调用——synchronized 锁线程，delay 需释放线程，两者冲突
            val cmd = synchronized(queue) {
                if (queue.isEmpty()) null
                else {
                    val next = queue.removeFirst()
                    _pendingCount.value = queue.size
                    next
                }
            }

            if (cmd == null) {
                delay(50)   // 队列空，短暂休眠（在 synchronized 外部，安全）
                continue
            }

            // 确保换行
            val data = if (cmd.endsWith('\n')) cmd else "$cmd\n"

            try {
                writer?.write(data)
                writer?.flush()
                _commandSent.emit(data.trim())
            } catch (e: Exception) {
                _connectionState.value = ConnectionState.Error(
                    "发送失败: ${e.message}")
                break
            }

            // 按指令类型等待
            delay(calculateDelay(data))
        }
    }

    // =========================================================================
    // 接收循环
    // =========================================================================

    /**
     * 持续读取服务器返回，通过 [commandAck] 发射。
     * 读到 null（对端关闭）或 IO 异常时退出。
     */
    private suspend fun receiveLoop() {
        try {
            while (coroutineContext.isActive) {
                val line = reader?.readLine() ?: break
                if (line.isNotBlank()) {
                    _commandAck.emit(line.trim())
                }
            }
        } catch (e: IOException) {
            if (coroutineContext.isActive) {
                _connectionState.value = ConnectionState.Error(
                    "连接断开: ${e.message}")
            }
        }
    }

    // =========================================================================
    // 延时计算
    // =========================================================================

    /**
     * 根据 GCode 指令类型返回发送后等待时间（毫秒）。
     *
     * | 指令类型              | 间隔   | 原因                 |
     * |----------------------|--------|---------------------|
     * | 设置类 (G17-G21 等)  |  50ms  | 瞬间执行              |
     * | Z 轴动作（抬/落笔）   | 500ms  | 短行程               |
     * | XY 运动 (G0/G1)      | 800ms  | 一般距离              |
     * | 圆弧 (G2/G3)         | 1500ms | 分段插值耗时长         |
     * | 延时 (G4 Px)         | P秒+100ms | 按指令参数         |
     * | 其他                 | 500ms  | 默认                 |
     */
    private fun calculateDelay(cmd: String): Long {
        val upper = cmd.uppercase().trim()

        return when {
            // G4 延时指令 —— 解析 P 参数
            upper.startsWith("G4") -> {
                val match = Regex("P(\\d+\\.?\\d*)").find(upper)
                val seconds = match?.groupValues?.get(1)?.toDoubleOrNull() ?: 0.0
                (seconds * 1000).toLong() + 100
            }

            // 圆弧
            upper.startsWith("G2") || upper.startsWith("G3") -> 1500

            // Z 轴动作（不含 X/Y，即纯 Z 指令）
            upper.contains("Z") && !upper.contains("X") && !upper.contains("Y") -> 500

            // XY 运动
            upper.startsWith("G0") || upper.startsWith("G1") -> 800

            // 设置类
            upper.startsWith("G17") || upper.startsWith("G18") ||
                    upper.startsWith("G19") || upper.startsWith("G20") ||
                    upper.startsWith("G21") || upper.startsWith("G90") ||
                    upper.startsWith("G91") -> 50

            // 默认
            else -> 500
        }
    }
}
