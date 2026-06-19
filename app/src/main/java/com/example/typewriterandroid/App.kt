package com.example.typewriterandroid.ui

import android.os.Build
import androidx.annotation.RequiresApi
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Gamepad
import androidx.compose.material.icons.filled.GridView
import androidx.compose.material.icons.filled.List
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import com.example.typewriterandroid.ui.component.ConnectBar
import com.example.typewriterandroid.ui.screen.GCodeScreen
import com.example.typewriterandroid.ui.screen.JogScreen
import com.example.typewriterandroid.ui.screen.LogScreen
import com.example.typewriterandroid.ui.screen.PresetScreen
import com.example.typewriterandroid.viewmodel.MainViewModel

// =========================================================================
// 导航页签
// =========================================================================

/** 顶层导航目标。 */
enum class Screen(val label: String) {
    JOG("手动"),
    GCODE("GCode"),
    PRESET("预设"),
    LOG("日志")
}

// =========================================================================
// App 入口
// =========================================================================

/**
 * TypewriterApp —— 顶层组装。
 *
 * ## 布局
 * ```
 * ┌──────────────────────────────┐
 * │  ConnectBar（IP / 端口 / 连接）│
 * ├──────────────────────────────┤
 * │                              │
 * │      当前 Screen              │
 * │                              │
 * ├──────────────────────────────┤
 * │  手动  │ GCode │ 预设 │ 日志  │
 * └──────────────────────────────┘
 * ```
 *
 * @param viewModel 由外部注入（预览时可替换为 remember { MainViewModel() }）
 */
@RequiresApi(Build.VERSION_CODES.O)
@Composable
fun TypewriterApp(viewModel: MainViewModel = remember { MainViewModel() }) {
    val client = viewModel.tcpClient
    val jog = viewModel.jogController
    val connectionState by client.connectionState.collectAsState()
    val pendingCount by client.pendingCount.collectAsState()
    var currentScreen by remember { mutableStateOf(Screen.JOG) }

    Scaffold(
        topBar = {
            ConnectBar(
                connectionState = connectionState,
                pendingCount = pendingCount,
                onConnect = { host, port -> viewModel.connect(host, port) },
                onDisconnect = { viewModel.disconnect() }
            )
        },
        bottomBar = {
            NavigationBar {
                NavigationBarItem(
                    selected = currentScreen == Screen.JOG,
                    onClick = { currentScreen = Screen.JOG },
                    icon = { Icon(Icons.Default.Gamepad, contentDescription = "手动") },
                    label = { Text("手动") }
                )
                NavigationBarItem(
                    selected = currentScreen == Screen.GCODE,
                    onClick = { currentScreen = Screen.GCODE },
                    icon = { Icon(Icons.Default.Terminal, contentDescription = "GCode") },
                    label = { Text("GCode") }
                )
                NavigationBarItem(
                    selected = currentScreen == Screen.PRESET,
                    onClick = { currentScreen = Screen.PRESET },
                    icon = { Icon(Icons.Default.GridView, contentDescription = "预设") },
                    label = { Text("预设") }
                )
                NavigationBarItem(
                    selected = currentScreen == Screen.LOG,
                    onClick = { currentScreen = Screen.LOG },
                    icon = { Icon(Icons.Default.List, contentDescription = "日志") },
                    label = { Text("日志") }
                )
            }
        }
    ) { padding ->
        Column(modifier = Modifier.padding(padding).fillMaxSize()) {
            when (currentScreen) {
                Screen.JOG    -> JogScreen(jog, client::sendCommand)
                Screen.GCODE  -> GCodeScreen(client, jog)
                Screen.PRESET -> PresetScreen(client)
                Screen.LOG    -> LogScreen(logEntries = viewModel.logEntries)
            }
        }
    }
}

// =========================================================================
// Preview
// =========================================================================

@RequiresApi(Build.VERSION_CODES.O)
@androidx.compose.ui.tooling.preview.Preview(showBackground = true)
@Composable
private fun PreviewTypewriterApp() {
    val vm = remember { MainViewModel() }
    TypewriterApp(viewModel = vm)
}
