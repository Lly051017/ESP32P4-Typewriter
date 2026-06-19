package com.example.typewriterandroid.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.typewriterandroid.tcp.ConnectionState
import androidx.compose.ui.tooling.preview.Preview


/**
 * 顶部连接栏 —— IP/端口输入 + 连接/断开按钮 + 状态指示灯 + 队列余量。
 */
@Composable
fun ConnectBar(
    connectionState: ConnectionState,
    pendingCount: Int,
    onConnect: (String, Int) -> Unit,
    onDisconnect: () -> Unit,
    modifier: Modifier = Modifier
) {
    var ip by remember { mutableStateOf("10.0.2.2") }
    var port by remember { mutableStateOf("8080") }

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .statusBarsPadding(),
        shadowElevation = 4.dp
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp)
        ) {
            // ---- 第一行：IP | 端口 | 按钮 ----
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                OutlinedTextField(
                    value = ip,
                    onValueChange = { ip = it },
                    enabled = connectionState.isDisconnected,
                    label = { Text("IP", fontSize = 12.sp) },
                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontSize = 14.sp),
                    singleLine = true,
                    modifier = Modifier.weight(3f)
                )
                OutlinedTextField(
                    value = port,
                    onValueChange = { port = it },
                    enabled = connectionState.isDisconnected,
                    label = { Text("端口", fontSize = 12.sp) },
                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontSize = 14.sp),
                    singleLine = true,
                    modifier = Modifier.weight(1.2f)
                )
                Button(
                    onClick = {
                        if (connectionState.isDisconnected || connectionState is ConnectionState.Error) {
                            onConnect(ip, port.toIntOrNull() ?: 8080)
                        } else {
                            onDisconnect()
                        }
                    },
                    modifier = Modifier.height(52.dp)
                ) {
                    Text(
                        if (connectionState.isDisconnected || connectionState is ConnectionState.Error) "连接"
                        else "断开",
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
            }

            // ---- 第二行：状态信息 ----
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                StatusDot(state = connectionState)
                if (pendingCount > 0) {
                    Spacer(Modifier.width(12.dp))
                    Text(
                        "队列: $pendingCount 条",
                        style = MaterialTheme.typography.labelSmall,
                        fontSize = 12.sp
                    )
                }
            }
        }
    }
}

/**
 * 状态指示灯 —— 颜色圆点 + 文字标签。
 */
@Composable
fun StatusDot(state: ConnectionState) {
    val color = when (state) {
        is ConnectionState.Disconnected -> Color(0xFF888888)
        is ConnectionState.Connecting   -> Color(0xFFFFAA00)
        is ConnectionState.Connected    -> Color(0xFF00CC00)
        is ConnectionState.Error        -> Color(0xFFFF0000)
    }
    val label = when (state) {
        is ConnectionState.Disconnected -> "未连接"
        is ConnectionState.Connecting   -> "连接中..."
        is ConnectionState.Connected    -> "已连接"
        is ConnectionState.Error        -> "错误"
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier
                .size(12.dp)
                .background(color, CircleShape)
        )
        Text(label, style = MaterialTheme.typography.labelSmall)
    }
}

@Preview(showBackground = true)
@Composable
private fun PreviewConnectBar() {
    ConnectBar(
        connectionState = ConnectionState.Disconnected,
        pendingCount = 3,
        onConnect = { _, _ -> },
        onDisconnect = {}
    )
}