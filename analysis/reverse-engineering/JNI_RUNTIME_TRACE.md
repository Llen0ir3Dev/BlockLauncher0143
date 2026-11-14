# JNI Runtime Trace — `libminecraftpe.so` 0.14.3

This document records **real runtime evidence** captured from the genuine
`libminecraftpe.so` (armeabi-v7a, from the 0.14.3 APK). It is not a static
guess: the library was actually loaded and `JNI_OnLoad` was executed.

## How it was captured (development only)

A synthetic `JavaVM`/`JNIEnv` harness (`compat/arm32/guest/jni_host.c`) was
cross-compiled for ARM32 with the Android NDK and run under `qemu-arm` on the
development machine. It `dlopen`s the genuine library, calls `JNI_OnLoad`, and
logs every JNI entry point the engine invokes.

This harness is a **development tool only**. It links against bionic and needs
a dynamic linker, so it is never shipped in the release AppImage. The shipped
path is the static bionic guest plus the in-tree ARM32 ELF loader.

Reproduce:

```sh
bash compat/arm32/tools/build_guest_arm32.sh --ndk "$ANDROID_NDK_HOME" --out build/guest-arm32
export LD_LIBRARY_PATH=original/libraries/armeabi-v7a:/system/lib
qemu-arm build/guest-arm32/jni_host_dynamic original/libraries/armeabi-v7a/libminecraftpe.so
```

## Result

| Property | Value |
| --- | --- |
| `dlopen(libminecraftpe.so)` | **OK** |
| `JNI_OnLoad` return value | **`0x10004`** (`65540` = `JNI_VERSION_1_4`) |
| Java classes bound | **15** |
| Instance methods bound (`GetMethodID`) | **59** |
| Static methods bound (`GetStaticMethodID`) | **10** |
| Instance fields bound (`GetFieldID`) | **3** |
| Static fields bound (`GetStaticFieldID`) | **1** |
| Global refs created (`NewGlobalRef`) | **12** |

`JNI_OnLoad` returning a non-negative version means the engine's entire JNI
binding phase completed successfully. This is the proof that the genuine native
library executes real code paths, not a simulation.

## Java API surface the engine requires

The Java side of the app (which NEXUS must provide as shims on Linux) must
expose the following. Class and method names/signatures are exactly as bound by
the engine.

### `com/mojang/minecraftpe/MainActivity`

Instance methods:

```
postScreenshotToFacebook  (Ljava/lang/String;II[I)V
getImageData              (Ljava/lang/String;Z)[I
getFileDataBytes          (Ljava/lang/String;)[B
displayDialog             (I)V
tick                      ()V
quit                      ()V
initiateUserInput         (I)V
getUserInputStatus        ()I
getUserInputString        ()[Ljava/lang/String;
getDateString             (I)Ljava/lang/String;
checkLicense              ()I
hasBuyButtonWhenInvalidLicense ()Z
buyGame                   ()V
vibrate                   (I)V
setIsPowerVR              (Z)V
isNetworkEnabled          (Z)Z
getPixelsPerMillimeter    ()F
getPlatformStringVar      (I)Ljava/lang/String;
```

### `com/mojang/minecraftpe/HardwareInformation`

Static methods:

```
saveScreenshot        (Ljava/lang/String;II[I)V
getDeviceModelName    ()Ljava/lang/String;
getAndroidVersion     ()Ljava/lang/String;
getCPUType            ()Ljava/lang/String;
getCPUName            ()Ljava/lang/String;
getCPUFeatures        ()Ljava/lang/String;
getNumCores           ()I
```

### Android framework classes (interactive input / window)

Bound through `MainActivity` inheritance:

```
android/view/Window
  getDecorView             ()Landroid/view/View;
  ...

android/content/Context
  getSystemService         (Ljava/lang/String;)Ljava/lang/Object;
  getWindow                ()Landroid/view/Window;
  ...

android/view/View
  getWindowToken           ()Landroid/os/IBinder;
  ...

android/view/inputmethod/InputMethodManager
  INPUT_METHOD_SERVICE     (static field, Ljava/lang/String;)
  showSoftInput            (Landroid/view/View;I)Z
  hideSoftInputFromWindow  (Landroid/os/IBinder;I)Z
  getKeyFromKeyCode        (III)I
  showKeyboard             (Ljava/lang/String;IZZ)V
  hideKeyboard             ()V
  getKeyboardHeight        ()F
  updateTextboxText        (Ljava/lang/String;)V
```

Additional application methods bound on the activity/context:

```
getAccessToken            ()Ljava/lang/String;
getClientId               ()Ljava/lang/String;
getProfileId              ()Ljava/lang/String;
getProfileName            ()Ljava/lang/String;
getBroadcastAddresses     ()[Ljava/lang/String;
getTotalMemory            ()J
launchUri                 (Ljava/lang/String;)V
pickImage                 (J)V
setFileDialogCallback     (J)V
getDeviceId               ()Ljava/lang/String;
createUUID                ()Ljava/lang/String;
```

### `com/mojang/android/net/HTTPRequest` / `HTTPResponse`

```
HTTPRequest.<init>        ()V
HTTPRequest.setURL        (Ljava/lang/String;)V
HTTPRequest.setRequestBody (Ljava/lang/String;)V
HTTPRequest.setCookieData (Ljava/lang/String;)V
HTTPRequest.setContentType (Ljava/lang/String;)V
HTTPRequest.send          (Ljava/lang/String;)Lcom/mojang/android/net/HTTPResponse;
HTTPRequest.abort         ()V

HTTPResponse.getStatus    ()I
HTTPResponse.getBody      ()Ljava/lang/String;
HTTPResponse.getResponseCode ()I
HTTPResponse.getHeaders   ()[Lorg/apache/http/Header;
```

### `org/apache/http/Header`

```
getName                   ()Ljava/lang/String;
getValue                  ()Ljava/lang/String;
```

### Store / billing (can be stubbed on Linux)

```
com/mojang/minecraftpe/store/NativeStoreListener
  <init>                  (J)V

com/mojang/minecraftpe/store/Product
  mId                     (field, Ljava/lang/String;)
  mPrice                  (field, Ljava/lang/String;)

com/mojang/minecraftpe/store/Purchase
  mProductId              (field, Ljava/lang/String;)

com/mojang/minecraftpe/store/StoreFactory
  createGooglePlayStore   (Ljava/lang/String;Lcom/mojang/minecraftpe/store/StoreListener;)Lcom/mojang/minecraftpe/store/Store;
  createAmazonAppStore    (Lcom/mojang/minecraftpe/store/StoreListener;)Lcom/mojang/minecraftpe/store/Store;
  createSamsungAppStore   (Lcom/mojang/minecraftpe/store/StoreListener;)Lcom/mojang/minecraftpe/store/Store;

com/mojang/minecraftpe/store/Store
  getStoreId              ()Ljava/lang/String;
  queryProducts           ([Ljava/lang/String;)V
  purchase                (Ljava/lang/String;)V
  queryPurchases          ()V
  destructor              ()V
```

### `java/lang/String`

Bound as a global reference (used for strings returned across JNI).

## Implications for NEXUS

1. **JNI layer**: NEXUS must implement a `JNIEnv`/`JavaVM` whose `FindClass`,
   `GetMethodID`, `GetStaticMethodID`, `GetFieldID`, `GetStaticFieldID`,
   `NewGlobalRef` and the call methods satisfy the above. The engine caches IDs
   at load time, so lookups only need to succeed once with stable handles.

2. **Lifecycle**: `tick()`, `quit()`, `isNetworkEnabled`, `getPixelsPerMillimeter`
   and the `InputMethodManager` methods are the minimum behaviour the engine
   expects from "the app". `ANativeActivity_onCreate`/`android_app` glue belongs
   here.

3. **Networking/billing**: `HTTPRequest`/`HTTPResponse`, `Store` and the license
   methods can be honest stubs for offline/single-player use.

4. **Hardware info**: return plausible Linux values (`getCPUType` → `"ARMv7"`,
   `getNumCores` → host cores, `getDeviceModelName` → a NEXUS identifier).

## Full raw trace

```
[jni] dlopen(libminecraftpe.so)
[jni] dlopen OK
[jni] calling JNI_OnLoad(vm=0x403c20)
[jni] FindClass("com/mojang/minecraftpe/MainActivity")
[jni] NewGlobalRef(0x1001)
[jni] FindClass("com/mojang/minecraftpe/HardwareInformation")
[jni] NewGlobalRef(0x1001)
[jni] GetStaticMethodID("saveScreenshot", "(Ljava/lang/String;II[I)V")
[jni] GetMethodID("postScreenshotToFacebook", "(Ljava/lang/String;II[I)V")
[jni] GetMethodID("getImageData", "(Ljava/lang/String;Z)[I")
[jni] GetMethodID("getFileDataBytes", "(Ljava/lang/String;)[B")
[jni] GetMethodID("displayDialog", "(I)V")
[jni] GetMethodID("tick", "()V")
[jni] GetMethodID("quit", "()V")
[jni] GetMethodID("initiateUserInput", "(I)V")
[jni] GetMethodID("getUserInputStatus", "()I")
[jni] GetMethodID("getUserInputString", "()[Ljava/lang/String;")
[jni] GetMethodID("getDateString", "(I)Ljava/lang/String;")
[jni] GetMethodID("checkLicense", "()I")
[jni] GetMethodID("hasBuyButtonWhenInvalidLicense", "()Z")
[jni] GetMethodID("buyGame", "()V")
[jni] GetMethodID("vibrate", "(I)V")
[jni] GetMethodID("setIsPowerVR", "(Z)V")
[jni] GetMethodID("isNetworkEnabled", "(Z)Z")
[jni] GetMethodID("getPixelsPerMillimeter", "()F")
[jni] GetMethodID("getPlatformStringVar", "(I)Ljava/lang/String;")
[jni] FindClass("android/view/Window")
[jni] NewGlobalRef(0x1001)
[jni] FindClass("android/content/Context")
[jni] NewGlobalRef(0x1001)
[jni] FindClass("android/view/View")
[jni] NewGlobalRef(0x1001)
[jni] FindClass("android/view/inputmethod/InputMethodManager")
[jni] NewGlobalRef(0x1001)
[jni] GetStaticFieldID("INPUT_METHOD_SERVICE", "Ljava/lang/String;")
[jni] GetMethodID("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;")
[jni] GetMethodID("getWindow", "()Landroid/view/Window;")
[jni] GetMethodID("getDecorView", "()Landroid/view/View;")
[jni] GetMethodID("showSoftInput", "(Landroid/view/View;I)Z")
[jni] GetMethodID("getWindowToken", "()Landroid/os/IBinder;")
[jni] GetMethodID("hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z")
[jni] GetMethodID("getKeyFromKeyCode", "(III)I")
[jni] GetMethodID("showKeyboard", "(Ljava/lang/String;IZZ)V")
[jni] GetMethodID("hideKeyboard", "()V")
[jni] GetMethodID("getKeyboardHeight", "()F")
[jni] GetMethodID("updateTextboxText", "(Ljava/lang/String;)V")
[jni] GetMethodID("getAccessToken", "()Ljava/lang/String;")
[jni] GetMethodID("getClientId", "()Ljava/lang/String;")
[jni] GetMethodID("getProfileId", "()Ljava/lang/String;")
[jni] GetMethodID("getProfileName", "()Ljava/lang/String;")
[jni] GetMethodID("getBroadcastAddresses", "()[Ljava/lang/String;")
[jni] GetMethodID("getTotalMemory", "()J")
[jni] GetMethodID("launchUri", "(Ljava/lang/String;)V")
[jni] GetMethodID("pickImage", "(J)V")
[jni] GetMethodID("setFileDialogCallback", "(J)V")
[jni] GetMethodID("getDeviceId", "()Ljava/lang/String;")
[jni] GetMethodID("createUUID", "()Ljava/lang/String;")
[jni] GetStaticMethodID("getDeviceModelName", "()Ljava/lang/String;")
[jni] GetStaticMethodID("getAndroidVersion", "()Ljava/lang/String;")
[jni] GetStaticMethodID("getCPUType", "()Ljava/lang/String;")
[jni] GetStaticMethodID("getCPUName", "()Ljava/lang/String;")
[jni] GetStaticMethodID("getCPUFeatures", "()Ljava/lang/String;")
[jni] GetStaticMethodID("getNumCores", "()I")
[jni] FindClass("com/mojang/android/net/HTTPResponse")
[jni] NewGlobalRef(0x1001)
[jni] GetMethodID("getStatus", "()I")
[jni] GetMethodID("getBody", "()Ljava/lang/String;")
[jni] GetMethodID("getResponseCode", "()I")
[jni] GetMethodID("getHeaders", "()[Lorg/apache/http/Header;")
[jni] FindClass("com/mojang/android/net/HTTPRequest")
[jni] NewGlobalRef(0x1001)
[jni] GetMethodID("<init>", "()V")
[jni] GetMethodID("setURL", "(Ljava/lang/String;)V")
[jni] GetMethodID("setRequestBody", "(Ljava/lang/String;)V")
[jni] GetMethodID("setCookieData", "(Ljava/lang/String;)V")
[jni] GetMethodID("setContentType", "(Ljava/lang/String;)V")
[jni] GetMethodID("send", "(Ljava/lang/String;)Lcom/mojang/android/net/HTTPResponse;")
[jni] GetMethodID("abort", "()V")
[jni] FindClass("org/apache/http/Header")
[jni] NewGlobalRef(0x1001)
[jni] GetMethodID("getName", "()Ljava/lang/String;")
[jni] GetMethodID("getValue", "()Ljava/lang/String;")
[jni] FindClass("com/mojang/minecraftpe/store/NativeStoreListener")
[jni] NewGlobalRef(0x1001)
[jni] GetMethodID("<init>", "(J)V")
[jni] FindClass("com/mojang/minecraftpe/store/Product")
[jni] GetFieldID("mId", "Ljava/lang/String;")
[jni] GetFieldID("mPrice", "Ljava/lang/String;")
[jni] FindClass("com/mojang/minecraftpe/store/Purchase")
[jni] GetFieldID("mProductId", "Ljava/lang/String;")
[jni] FindClass("com/mojang/minecraftpe/store/StoreFactory")
[jni] NewGlobalRef(0x1001)
[jni] GetStaticMethodID("createGooglePlayStore", "(Ljava/lang/String;Lcom/mojang/minecraftpe/store/StoreListener;)Lcom/mojang/minecraftpe/store/Store;")
[jni] GetStaticMethodID("createAmazonAppStore", "(Lcom/mojang/minecraftpe/store/StoreListener;)Lcom/mojang/minecraftpe/store/Store;")
[jni] GetStaticMethodID("createSamsungAppStore", "(Lcom/mojang/minecraftpe/store/StoreListener;)Lcom/mojang/minecraftpe/store/Store;")
[jni] FindClass("com/mojang/minecraftpe/store/Store")
[jni] GetMethodID("getStoreId", "()Ljava/lang/String;")
[jni] GetMethodID("queryProducts", "([Ljava/lang/String;)V")
[jni] GetMethodID("purchase", "(Ljava/lang/String;)V")
[jni] GetMethodID("queryPurchases", "()V")
[jni] GetMethodID("destructor", "()V")
[jni] FindClass("java/lang/String")
[jni] NewGlobalRef(0x1001)
[jni] JNI_OnLoad returned 0x10004 (65540)
```
