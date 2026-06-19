package com.example.typewriterandroid.ui.component

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp

/**
 * 步长选择器 —— 1 / 5 / 10 / 50 mm。
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StepSelector(
    value: Double,
    onValueChange: (Double) -> Unit,
    modifier: Modifier = Modifier
) {
    val steps = listOf(1.0, 5.0, 10.0, 50.0)
    Row(
        modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(4.dp, Alignment.CenterHorizontally)
    ) {
        Text("步长:", modifier = Modifier.align(Alignment.CenterVertically))
        steps.forEach { step ->
            FilterChip(
                selected = value == step,
                onClick = { onValueChange(step) },
                label = { Text("${step.toInt()}mm") }
            )
        }
    }
}

@Preview(showBackground = true)
@Composable
private fun PreviewSpeedSelector() {
    SpeedSelector(value = 1000, onValueChange = {})
}