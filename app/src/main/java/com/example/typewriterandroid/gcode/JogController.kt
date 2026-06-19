package com.example.typewriterandroid.gcode

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * JOG 手动控制器 —— 步长 / 速度管理 + 坐标跟踪 + 指令生成。
 *
 * ## 架构
 *  - StateFlow：驱动 UI 自动刷新（替代 Qt signal）
 *  - SharedFlow：指令输出，由外部收集后喂给 TcpClient
 *  - `tryEmit`：非挂起发射，调用方不阻塞
 *
 * ## 用法（ViewModel / Composable 层）
 *   val jc = JogController()
 *   jc.setStep(10.0)
 *   jc.setSpeed(2000)
 *   jc.jog("X", 1)          // X 正方向走 step 距离
 *   jc.penDown()            // 落笔
 *   jc.home()               // 归零
 *
 *   // 收集指令喂给 TcpClient
 *   jc.command.collect { cmd -> tcpClient.sendCommand(cmd) }
 */
class JogController {

    // =========================================================================
    // 可观察状态（StateFlow → UI 自动刷新）
    // =========================================================================

    private val _step = MutableStateFlow(5.0)
    val step: StateFlow<Double> = _step.asStateFlow()

    private val _speed = MutableStateFlow(1000)
    val speed: StateFlow<Int> = _speed.asStateFlow()

    private val _posX = MutableStateFlow(0.0)
    val posX: StateFlow<Double> = _posX.asStateFlow()

    private val _posY = MutableStateFlow(0.0)
    val posY: StateFlow<Double> = _posY.asStateFlow()

    private val _posZ = MutableStateFlow(5.0)
    val posZ: StateFlow<Double> = _posZ.asStateFlow()

    private val _isPenDown = MutableStateFlow(false)
    val isPenDown: StateFlow<Boolean> = _isPenDown.asStateFlow()

    // =========================================================================
    // 指令输出流（替代 Qt signal jogCommand）
    // =========================================================================

    private val _command = MutableSharedFlow<String>(extraBufferCapacity = 16)
    val command: SharedFlow<String> = _command

    // =========================================================================
    // 控制方法
    // =========================================================================

    fun setStep(value: Double) {
        _step.value = value
    }

    fun setSpeed(value: Int) {
        _speed.value = value
    }

    /**
     * 沿指定轴移动。
     *
     * @param axis  "X" | "Y" | "HOME"
     * @param direction  1（正方向）| -1（负方向）| 0（HOME 时忽略）
     */
    fun jog(axis: String, direction: Int) {
        var targetX = _posX.value
        var targetY = _posY.value

        when (axis) {
            "X" -> targetX = (_posX.value + _step.value * direction)
                .coerceIn(0.0, 200.0)
            "Y" -> targetY = (_posY.value + _step.value * direction)
                .coerceIn(0.0, 200.0)
            "HOME" -> {
                targetX = 0.0
                targetY = 0.0
            }
        }

        val cmd = if (_isPenDown.value) {
            "G1 X${targetX.fmt()} Y${targetY.fmt()} F${_speed.value}"
        } else {
            "G0 X${targetX.fmt()} Y${targetY.fmt()}"
        }

        // 软件跟踪（不等硬件确认）—— StateFlow 更新自动通知 UI
        _posX.value = targetX
        _posY.value = targetY
        _command.tryEmit(cmd)
    }

    fun penUp() {
        _isPenDown.value = false
        _command.tryEmit("G0 Z5")
    }

    fun penDown() {
        _isPenDown.value = true
        _command.tryEmit("G1 Z0 F1000")
    }

    fun stop() {
        _command.tryEmit("M0")
    }

    fun home() {
        _posX.value = 0.0
        _posY.value = 0.0
        _posZ.value = 5.0
        _command.tryEmit("G0 X0 Y0")
    }

    // =========================================================================
    // 服务器位置反馈（由 MainViewModel 解析后调用）
    // =========================================================================

    /** 从服务器状态回执更新当前位置（硬件真值）。 */
    fun setPosition(x: Double, y: Double, z: Double) {
        _posX.value = x
        _posY.value = y
        _posZ.value = z
    }

    /** 从服务器状态回执更新笔状态。 */
    fun setPenDown(down: Boolean) {
        _isPenDown.value = down
    }

    // =========================================================================
    // 内部：数值格式化
    // =========================================================================

    companion object {
        private fun Double.fmt() = "%.1f".format(this)
    }
}
