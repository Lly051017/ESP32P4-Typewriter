package com.example.typewriterandroid

import com.example.typewriterandroid.gcode.GCodeBuilder
import com.example.typewriterandroid.gcode.JogController
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking

fun main() = runBlocking {
    // =========================================================================
    // 1. GCodeBuilder 测试
    // =========================================================================
    println("=" .repeat(50))
    println("GCodeBuilder 测试")
    println("=".repeat(50))

    println("unitMM()           → ${GCodeBuilder.unitMM()}")
    println("absolute()         → ${GCodeBuilder.absolute()}")
    println("planeXY()          → ${GCodeBuilder.planeXY()}")
    println("penUp()            → ${GCodeBuilder.penUp()}")
    println("penDown(1500)      → ${GCodeBuilder.penDown(1500)}")
    println("rapid(10, 20)      → ${GCodeBuilder.rapid(10.0, 20.0)}")
    println("linear(50,60,2000) → ${GCodeBuilder.linear(50.0, 60.0, 2000)}")
    println("arcCW(30,40,5,5,1500) → ${GCodeBuilder.arcCW(30.0, 40.0, 5.0, 5.0, 1500)}")
    println("dwell(2.5)         → ${GCodeBuilder.dwell(2.5)}")
    println("stop()             → ${GCodeBuilder.stop()}")
    println("home()             → ${GCodeBuilder.home()}")

    println()
    println("--- square(0, 0, 10, 1000) ---")
    GCodeBuilder.square(0.0, 0.0, 10.0, 1000).forEach { println("  $it") }

    println()
    println("--- triangle(0,0, 20,0, 10,15, 800) ---")
    GCodeBuilder.triangle(0.0, 0.0, 20.0, 0.0, 10.0, 15.0, 800).forEach { println("  $it") }

    println()
    println("--- circleApprox(50, 50, 20, 12, 1000) ---")
    val circle = GCodeBuilder.circleApprox(50.0, 50.0, 20.0, 12, 1000)
    println("  共 ${circle.size} 条指令")
    circle.take(5).forEach { println("  $it") }
    println("  ...")
    circle.takeLast(2).forEach { println("  $it") }

    // =========================================================================
    // 2. JogController 测试
    // =========================================================================
    println()
    println("=".repeat(50))
    println("JogController 测试")
    println("=".repeat(50))

    val jc = JogController()

    // 收集指令
    val job = launch {
        jc.command.collect { println("[JOG 指令] $it") }
    }

    // 初始状态
    println("初始位置: X=${jc.posX.value} Y=${jc.posY.value} Z=${jc.posZ.value} 落笔=${jc.isPenDown.value}")

    // 抬笔移动
    jc.setStep(10.0)
    jc.setSpeed(2000)
    println("\n>>> jog X+ 两次（抬笔）")
    jc.jog("X", 1)
    jc.jog("X", 1)
    delay(50)

    println("位置: X=${jc.posX.value} Y=${jc.posY.value}")

    // 落笔移动
    println("\n>>> penDown → jog Y+")
    jc.penDown()
    jc.jog("Y", 1)
    delay(50)

    println("位置: X=${jc.posX.value} Y=${jc.posY.value} 落笔=${jc.isPenDown.value}")

    // 抬笔
    println("\n>>> penUp")
    jc.penUp()
    delay(50)

    // 边界测试
    println("\n>>> jog X+ 30 次（测边界 200）")
    jc.setStep(10.0)
    repeat(30) { jc.jog("X", 1) }
    delay(50)
    println("位置: X=${jc.posX.value}（上限 200）")

    // HOME
    println("\n>>> home()")
    jc.home()
    delay(50)
    println("位置: X=${jc.posX.value} Y=${jc.posY.value} Z=${jc.posZ.value}")

    job.cancel()

    println()
    println("=".repeat(50))
    println("全部测试完成 ✅")
    println("=".repeat(50))
}
