package com.example.typewriterandroid.ui.screen

import android.os.Build
import androidx.annotation.RequiresApi
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.time.LocalTime
import java.time.format.DateTimeFormatter

// =========================================================================
// 数据类
// =========================================================================

/** 日志级别，自带对应颜色。 */
enum class LogType(val color: Color) {
    SEND(Color(0xFF00AA00)),  // 发送 → 绿色
    RECV(Color(0xFF0066FF)),  // 接收 → 蓝色
    INFO(Color(0xFF888888)),  // 信息 → 灰色
    ERROR(Color(0xFFFF0000))  // 错误 → 红色
}

/** 一条日志记录。 */
data class LogEntry(
    val timestamp: String,   // HH:mm:ss
    val direction: String,   // "→" / "←" / ""
    val message: String,
    val type: LogType
)

// =========================================================================
// 通信日志页面
// =========================================================================

/**
 * 通信日志页面 —— 实时显示发送/接收/连接状态，带颜色标记。
 *
 * 日志收集由 [com.example.typewriterandroid.viewmodel.MainViewModel] 完成，
 * 本页面只负责展示，确保切换页签时日志不丢失。
 *
 * ## 布局
 *  1. 标题行
 *  2. 操作按钮（清空 / 导出）
 *  3. LazyColumn 日志列表，最新在上
 *
 * @param logEntries 日志列表（由 ViewModel 维护的 SnapshotStateList）
 * @param onExport   导出回调，传入全部日志文本
 * @param modifier   根修饰符
 */
@RequiresApi(Build.VERSION_CODES.O)
@Composable
fun LogScreen(
    logEntries: List<LogEntry>,
    onExport: (String) -> Unit = {},
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        // ---- 标题 + 操作按钮 ----
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text("通信日志", style = MaterialTheme.typography.headlineSmall)
            Row {
                TextButton(onClick = {
                    if (logEntries is androidx.compose.runtime.snapshots.SnapshotStateList) {
                        logEntries.clear()
                    }
                }) {
                    Text("清空")
                }
                TextButton(onClick = {
                    val text = logEntries.joinToString("\n") { entry ->
                        "[${entry.timestamp}] ${entry.direction} ${entry.message}".trim()
                    }
                    onExport(text)
                }) {
                    Text("导出")
                }
            }
        }

        Spacer(Modifier.height(8.dp))

        // ---- 日志列表 ----
        LazyColumn(modifier = Modifier.weight(1f)) {
            items(logEntries) { entry ->
                Row(Modifier.padding(vertical = 2.dp)) {
                    Text(
                        "[${entry.timestamp}] ",
                        color = Color.Gray,
                        fontSize = 13.sp,
                        fontFamily = FontFamily.Monospace
                    )
                    Text(
                        text = if (entry.direction.isEmpty()) entry.message
                        else "${entry.direction} ${entry.message}",
                        color = entry.type.color,
                        fontSize = 13.sp,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
        }
    }
}

// =========================================================================
// Preview（模拟数据）
// =========================================================================

@androidx.compose.ui.tooling.preview.Preview(showBackground = true)
@RequiresApi(Build.VERSION_CODES.O)
@Composable
private fun PreviewLogScreen() {
    val demoLogs = remember {
        mutableStateListOf(
            LogEntry("14:30:01", "→", "G21", LogType.SEND),
            LogEntry("14:30:01", "←", "ok", LogType.RECV),
            LogEntry("14:30:02", "→", "G90", LogType.SEND),
            LogEntry("14:30:02", "←", "ok", LogType.RECV),
            LogEntry("14:30:03", "", "已连接", LogType.INFO),
        )
    }
    LogScreen(logEntries = demoLogs)
}
