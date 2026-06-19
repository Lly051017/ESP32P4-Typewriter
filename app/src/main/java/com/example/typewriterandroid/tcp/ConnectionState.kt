package com.example.typewriterandroid.tcp

/**
 * TCP 连接状态 —— 密封类，每个状态可携带不同数据。
 *
 * 用法（Compose UI）：
 *   val state by tcpClient.connectionState.collectAsState()
 *   when (state) {
 *       is ConnectionState.Disconnected -> 灰色指示灯
 *       is ConnectionState.Connecting   -> 黄色指示灯
 *       is ConnectionState.Connected    -> 绿色指示灯
 *       is ConnectionState.Error        -> 红色指示灯 + message
 *   }
 *
 * 编译器强制 when 覆盖所有分支（没有 else 时），新增状态会直接报编译错误。
 */
sealed class ConnectionState {

    /** 未连接（初始状态 / 断开后） */
    data object Disconnected : ConnectionState()

    /** 连接中（connect() 调用后、TCP 握手期间） */
    data object Connecting : ConnectionState()

    /** 已连接（TCP 握手成功） */
    data object Connected : ConnectionState()

    /** 错误（连接失败 / 通信中断），携带错误描述 */
    data class Error(val message: String) : ConnectionState()

    // === 便捷判断（Compose UI 中直接使用） ===

    val isConnected: Boolean get() = this is Connected
    val isDisconnected: Boolean get() = this is Disconnected
}
