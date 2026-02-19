# Add project specific ProGuard rules here.
# By default, the flags in this file are appended to flags specified
# in ${sdk.dir}/tools/proguard/proguard-android.txt

# Keep native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep UNAudio native library
-keep class com.unaudio.** { *; }
