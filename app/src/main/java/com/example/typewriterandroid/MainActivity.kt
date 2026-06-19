package com.example.typewriterandroid

import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.annotation.RequiresApi
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.ui.Modifier
import com.example.typewriterandroid.ui.TypewriterApp

/**
 * 唯一 Activity —— 启动即进入 [TypewriterApp]。
 *
 * 原先的 TCP/GCode 测试按钮已移除，
 * 所有页面通过 App.kt 内的 NavigationRail 切换。
 */
class MainActivity : ComponentActivity() {
    @RequiresApi(Build.VERSION_CODES.O)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            TypewriterApp()
        }
    }
}
