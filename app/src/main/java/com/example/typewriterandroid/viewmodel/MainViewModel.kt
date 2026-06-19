package com.example.typewriterandroid.viewmodel

import android.os.Build
import androidx.annotation.RequiresApi
import androidx.compose.runtime.mutableStateListOf
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.typewriterandroid.gcode.JogController
import com.example.typewriterandroid.tcp.ConnectionState
import com.example.typewriterandroid.tcp.TcpClient
import com.example.typewriterandroid.ui.screen.LogEntry
import com.example.typewriterandroid.ui.screen.LogType
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.time.LocalTime
import java.time.format.DateTimeFormatter

/**
 * 全局 ViewModel —— 持有底层模块 + 日志列表 + 连接管理。
 *
 * ## 职责
 *  - 创建并持有 [TcpClient] 与 [JogController]
 *  - 收集三大流到 [logEntries]，跨页签持久存在
 *  - 提供 connect/disconnect 方法
 *  - onCleared 时自动断开连接
 *
 * ## 信号连接
 *  JogController → TcpClient 的指令转发由 [com.example.typewriterandroid.ui.screen.JogScreen]
 *  内部的 [LaunchedEffect] 完成，ViewModel 不重复收集，避免 SharedFlow 双重发送。
 */
@RequiresApi(Build.VERSION_CODES.O)
class MainViewModel : ViewModel() {

    val tcpClient = TcpClient()
    val jogController = JogController()

    // =========================================================================
    // 持久日志列表（跨页签不丢失）
    // =========================================================================

    /** 所有日志，最新在前。Compose SnapshotStateList，UI 自动刷新。 */
    val logEntries = mutableStateListOf<LogEntry>()

    init {
        // 发送指令 → 日志
        viewModelScope.launch {
            tcpClient.commandSent.collect { cmd ->
                logEntries.add( LogEntry(now(), "→", cmd, LogType.SEND))
            }
        }
        // 服务器响应 → 日志
        viewModelScope.launch {
            tcpClient.commandAck.collect { resp ->
                logEntries.add( LogEntry(now(), "←", resp, LogType.RECV))
            }
        }
        // 连接状态变更 → 日志
        viewModelScope.launch {
            tcpClient.connectionState.collect { state ->
                val msg = when (state) {
                    is ConnectionState.Disconnected -> "已断开"
                    is ConnectionState.Connecting   -> "正在连接..."
                    is ConnectionState.Connected    -> "已连接"
                    is ConnectionState.Error        -> "错误: ${state.message}"
                }
                val type = if (state is ConnectionState.Error) LogType.ERROR
                else LogType.INFO
                logEntries.add( LogEntry(now(), "", msg, type))
            }
        }
        // 服务器位置反馈 → 更新 JogController 当前位置
        viewModelScope.launch {
            // 匹配 GRBL 状态行: <Idle|MPos:10.000,20.000,5.000>
            val posPattern = Regex("""<Idle\|MPos:([\d.\-]+),([\d.\-]+),([\d.\-]+)>""")
            tcpClient.commandAck.collect { line ->
                posPattern.find(line)?.let { match ->
                    val x = match.groupValues[1].toDoubleOrNull()
                    val y = match.groupValues[2].toDoubleOrNull()
                    val z = match.groupValues[3].toDoubleOrNull()
                    if (x != null && y != null && z != null) {
                        jogController.setPosition(x, y, z)
                        jogController.setPenDown(z < 2.0) // Z < 2mm 视为落笔
                    }
                }
            }
        }
    }

    // =========================================================================
    // 连接管理
    // =========================================================================

    /** 发起 TCP 连接（IO 线程，不阻塞 UI）。 */
    fun connect(host: String, port: Int) {
        viewModelScope.launch {
            try {
                withContext(Dispatchers.IO) {
                    tcpClient.connect(host, port)
                }
            } catch (_: Exception) {
                // 错误已通过 TcpClient._connectionState → Error 反映到 UI
            }
        }
    }

    /** 断开 TCP 连接。 */
    fun disconnect() {
        tcpClient.disconnect()
    }

    // =========================================================================
    // 生命周期
    // =========================================================================

    override fun onCleared() {
        super.onCleared()
        tcpClient.disconnect()
    }

    // =========================================================================
    // 内部
    // =========================================================================

    private fun now() = LocalTime.now()
        .format(DateTimeFormatter.ofPattern("HH:mm:ss"))
}
