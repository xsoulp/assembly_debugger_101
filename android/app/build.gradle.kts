plugins { id("com.android.application") }

android {
    namespace = "com.xsoulp.asm101"
    compileSdk = 36
    ndkVersion = "27.3.13750724"
    providers.gradleProperty("asm101.ndkPath").orNull?.let { ndkPath = it }
    defaultConfig {
        applicationId = "com.xsoulp.asm101"
        minSdk = 28
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += "arm64-v8a" }
        testInstrumentationRunner = "com.xsoulp.asm101.NativeDebuggerInstrumentation"
    }
    val localRelease = signingConfigs.create("localRelease") {
        val signingDir = rootProject.file(".signing")
        val key = signingDir.resolve("asm101-release.jks")
        val passwordFile = signingDir.resolve("password")
        if (key.exists() && passwordFile.exists()) {
            storeFile = key
            storePassword = passwordFile.readText().trim()
            keyAlias = "asm101"
            keyPassword = storePassword
        }
    }
    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
            if (localRelease.storeFile != null) signingConfig = localRelease
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    packaging { jniLibs.useLegacyPackaging = false }
}

val compileNative = tasks.register<Exec>("compileNative") {
    val requestedNdk = providers.gradleProperty("asm101.ndkPath")
        .orElse(providers.environmentVariable("ANDROID_NDK_HOME"))
    inputs.files(fileTree("src/main/cpp"))
    inputs.file(rootProject.file("scripts/build-native.sh"))
    inputs.property("ndkPath", requestedNdk.orElse("SDK default"))
    outputs.file("src/main/jniLibs/arm64-v8a/libasm101.so")
    commandLine("bash", rootProject.file("scripts/build-native.sh").absolutePath, requestedNdk.orElse("").get())
}
tasks.named("preBuild").configure { dependsOn(compileNative) }
