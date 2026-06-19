plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")         // Compose 编译器插件
}

android {
    namespace = "com.example.typewriterandroid"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.example.typewriterandroid"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    kotlinOptions {
        jvmTarget = "11"
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    // === 项目模板自带（通过版本目录 libs 管理） ===
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)

    // === 本项目新增依赖 ===

    // 扩展图标库（Gamepad / Terminal / GridView 需要）
    // 版本由 BOM 统一管理，不要写死版本号
    implementation("androidx.compose.material:material-icons-extended")

    // Lifecycle ViewModel Compose（MainViewModel 基类）
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")

    // Lifecycle Runtime Compose（collectAsState 需要）
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")

    // Coroutines（TCP 异步通信核心——替代 QTimer + 信号/槽）
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")

    // === 测试依赖（模板自带，保留不动） ===
    testImplementation(libs.junit)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
    debugImplementation(libs.androidx.compose.ui.tooling)
}