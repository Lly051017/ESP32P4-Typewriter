package com.example.typewriterandroid.ui.screen

import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.RequiresApi
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.typewriterandroid.gcode.GCodeBuilder
import com.example.typewriterandroid.gcode.JogController
import com.example.typewriterandroid.tcp.TcpClient
import java.time.LocalTime
import java.time.format.DateTimeFormatter

// =========================================================================
// 数据类
// =========================================================================

/**
 * 指令历史条目。
 *
 * @param index     序号（从 1 开始）
 * @param command   已发送的 GCode 指令原文
 * @param timestamp 发送时间 HH:mm:ss
 */
data class CommandEntry(
    val index: Int,
    val command: String,
    val timestamp: String
)

// =========================================================================
// 快捷按钮
// =========================================================================

/**
 * 紧凑型快捷按钮，用于高频指令一键发送。
 *
 * @param label   按钮文字
 * @param onClick 点击回调
 */
@Composable
fun QuickButton(label: String, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 4.dp),
        modifier = Modifier.height(36.dp)
    ) {
        Text(label, fontSize = 13.sp)
    }
}

// =========================================================================
// GCode 终端页面
// =========================================================================

/**
 * GCode 终端页面 —— 快捷指令 + 手动输入 + 发送历史 + 文件发送。
 *
 * ## 布局（从上到下）
 *  1. 快捷按钮行：G21 / G90 / 抬笔 / 落笔 / 回零 / STOP
 *  2. 输入行：OutlinedTextField + 发送按钮
 *  3. 发送历史：LazyColumn，最新在上
 *  4. 文件操作：选择文件 + 停止发送
 *
 * ## 信号链
 *   - 快捷按钮 → tcpClient.sendCommand() / jogController.*()
 *   - 手动输入 → tcpClient.sendCommand(cmd)
 *   - tcpClient.commandSent → history 列表
 *
 * @param tcpClient     TCP 客户端，提供发送接口与 commandSent 流
 * @param jogController JOG 控制器，提供抬落笔/回零/停止
 * @param modifier      根修饰符
 */
@RequiresApi(Build.VERSION_CODES.O)
@Composable
fun GCodeScreen(
    tcpClient: TcpClient,
    jogController: JogController,
    modifier: Modifier = Modifier
) {
    var cmdInput by remember { mutableStateOf("") }
    val history = remember { mutableStateListOf<CommandEntry>() }
    val context = LocalContext.current
    val fileLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        val lines = GCodeBuilder.readGCodeUri(context, uri)
        if (lines.isNotEmpty()) {
            tcpClient.sendCommands(lines)
        }
    }

    // JogController 指令 → TcpClient (抬笔/落笔/回零/STOP)
    LaunchedEffect(Unit) {
        jogController.command.collect { cmd ->
            tcpClient.sendCommand(cmd)
        }
    }

    // 收集已发送指令 → 历史列表
    LaunchedEffect(Unit) {
        tcpClient.commandSent.collect { cmd ->
            history.add(
                0,
                CommandEntry(
                    index = history.size + 1,
                    command = cmd,
                    timestamp = LocalTime.now()
                        .format(DateTimeFormatter.ofPattern("HH:mm:ss"))
                )
            )
        }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        // ---- 标题 ----
        Text("GCode 终端", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(12.dp))

        // ---- 快捷按钮行 ----
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            QuickButton("G21") { tcpClient.sendCommand(GCodeBuilder.unitMM()) }
            QuickButton("G90") { tcpClient.sendCommand(GCodeBuilder.absolute()) }
            QuickButton("抬笔") { jogController.penUp() }
            QuickButton("落笔") { jogController.penDown() }
            QuickButton("回零") { jogController.home() }
            QuickButton("STOP") { jogController.stop() }
        }

        Spacer(Modifier.height(12.dp))

        // ---- 输入行 ----
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            OutlinedTextField(
                value = cmdInput,
                onValueChange = { cmdInput = it },
                placeholder = { Text("输入 GCode 指令...") },
                singleLine = true,
                modifier = Modifier.weight(1f)
            )
            Button(onClick = {
                if (cmdInput.isNotBlank()) {
                    tcpClient.sendCommand(cmdInput.trim())
                    cmdInput = ""
                }
            }) {
                Text("发送")
            }
        }

        Spacer(Modifier.height(12.dp))

        // ---- 发送历史 ----
        Text("发送历史", style = MaterialTheme.typography.titleSmall)
        Spacer(Modifier.height(4.dp))

        LazyColumn(modifier = Modifier.weight(1f)) {
            items(history) { entry ->
                Row(
                    Modifier.padding(vertical = 2.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        "#${entry.index}",
                        color = Color.Gray,
                        fontFamily = FontFamily.Monospace,
                        fontSize = 12.sp
                    )
                    Spacer(Modifier.width(4.dp))
                    Text(
                        "✓",
                        color = Color(0xFF00AA00),
                        fontSize = 12.sp
                    )
                    Spacer(Modifier.width(4.dp))
                    Text(
                        entry.command,
                        modifier = Modifier.weight(1f),
                        fontFamily = FontFamily.Monospace,
                        fontSize = 13.sp
                    )
                    Text(
                        entry.timestamp,
                        color = Color.Gray,
                        fontSize = 12.sp
                    )
                }
            }
        }

        Spacer(Modifier.height(8.dp))

        // ---- 文件操作 ----
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = { fileLauncher.launch(arrayOf("*/*")) }) {
                Text("选择文件")
            }
            Button(
                onClick = { tcpClient.clearQueue() },
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.error
                )
            ) {
                Text("停止发送")
            }
        }
    }
}

// =========================================================================
// Preview
// =========================================================================

@androidx.compose.ui.tooling.preview.Preview(showBackground = true)
@RequiresApi(Build.VERSION_CODES.O)
@Composable
private fun PreviewGCodeScreen() {
    val tc = remember { TcpClient() }
    val jc = remember { JogController() }
    GCodeScreen(tcpClient = tc, jogController = jc)
}
