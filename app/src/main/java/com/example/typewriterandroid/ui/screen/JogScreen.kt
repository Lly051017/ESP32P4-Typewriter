package com.example.typewriterandroid.ui.screen

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.example.typewriterandroid.gcode.JogController
import com.example.typewriterandroid.ui.component.JogPad
import com.example.typewriterandroid.ui.component.SpeedSelector
import com.example.typewriterandroid.ui.component.StepSelector

/**
 * JOG 手动控制页面 —— 方向键 + 抬落笔 + 步长速度 + 位置显示。
 *
 * ## 信号链
 *   JogController.command  →  TcpClient.sendCommand
 *   在 Screen 或 ViewModel 层通过 LaunchedEffect 连接：
 *   ```
 *   LaunchedEffect(Unit) {
 *       jogController.command.collect { cmd ->
 *           tcpClient.sendCommand(cmd)
 *       }
 *   }
 *   ```
 *
 * @param jogController  JOG 控制器，驱动所有状态
 * @param onSendCommand  指令输出回调，连接 TcpClient.sendCommand
 */
@Composable
fun JogScreen(
    jogController: JogController,
    onSendCommand: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    // 直接观察 Controller 的 StateFlow，状态变化时自动重组
    val step by jogController.step.collectAsState()
    val speed by jogController.speed.collectAsState()
    val posX by jogController.posX.collectAsState()
    val posY by jogController.posY.collectAsState()
    val posZ by jogController.posZ.collectAsState()
    val isPenDown by jogController.isPenDown.collectAsState()

    // 将 JogController 指令输出连接到 TcpClient
    LaunchedEffect(Unit) {
        jogController.command.collect { cmd ->
            onSendCommand(cmd)
        }
    }

    Column(
        modifier = modifier.padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // ---- 标题 ----
        Text("手动控制", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(16.dp))

        // ---- 方向键 ----
        JogPad(onJog = { axis, dir -> jogController.jog(axis, dir) })

        Spacer(Modifier.height(16.dp))

        // ---- 抬笔 / 落笔 / 停止 ----
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            Button(onClick = { jogController.penUp() }) {
                Text("抬笔")
            }
            Button(onClick = { jogController.penDown() }) {
                Text("落笔")
            }
            Button(
                onClick = { jogController.stop() },
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.error
                )
            ) {
                Text("STOP")
            }
        }

        Spacer(Modifier.height(16.dp))

        // ---- 步长 / 速度 ----
        StepSelector(value = step, onValueChange = { jogController.setStep(it) })
        SpeedSelector(value = speed, onValueChange = { jogController.setSpeed(it) })

        Spacer(Modifier.height(16.dp))

        // ---- 当前位置显示 ----
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp)) {
                Text("当前位置", style = MaterialTheme.typography.titleSmall)
                Text("X: ${posX.fmt()} mm")
                Text("Y: ${posY.fmt()} mm")
                Text("Z: ${posZ.fmt()} mm")
                Text("笔状态: ${if (isPenDown) "落笔" else "抬笔"}")
            }
        }
    }
}

// =========================================================================
// 内部：数值格式化
// =========================================================================

private fun Double.fmt() = "%.1f".format(this)

@Preview(showBackground = true)
@Composable
private fun PreviewJogScreen() {
    val jc = remember { JogController() }
    JogScreen(
        jogController = jc,
        onSendCommand = {}
    )
}
