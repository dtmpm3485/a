from pathlib import Path
import json

root = Path("launcher")

p = root / "app/src/main/cpp/CMakeLists.txt"
s = p.read_text()
patch = """
enable_language(C)
find_package(shadowhook REQUIRED CONFIG)

add_library(mcesp2644 SHARED mcesp2644.c)
target_link_libraries(mcesp2644 PRIVATE android log m shadowhook::shadowhook)
"""
if "add_library(mcesp2644" not in s:
    s += "\n" + patch + "\n"
p.write_text(s)

p = root / "app/build.gradle"
s = p.read_text()
needle = "dependencies {"
if "com.bytedance.android:shadowhook:2.0.0" not in s:
    s = s.replace(needle, needle + '\n    implementation "com.bytedance.android:shadowhook:2.0.0"')
p.write_text(s)

p = root / "app/src/main/java/org/levimc/launcher/core/minecraft/MinecraftActivity.kt"
s = p.read_text()
old = """        if (overlayManager == null) {
            startInbuiltModServices()
        }
    }"""
new = """        if (overlayManager == null) {
            startInbuiltModServices()
        }
        EspOverlayView.attachAndInstall(this)
    }"""
if old not in s:
    raise SystemExit("MinecraftActivity onResume block not found")
s = s.replace(old, new, 1)
p.write_text(s)

gs = {
    "project_info": {
        "project_number": "000000000000",
        "project_id": "mc-esp-ci",
        "storage_bucket": "mc-esp-ci.appspot.com",
    },
    "client": [{
        "client_info": {
            "mobilesdk_app_id": "1:000000000000:android:0000000000000000000000",
            "android_client_info": {"package_name": "org.levimc.launcher"},
        },
        "oauth_client": [],
        "api_key": [{"current_key": "AIzaSyCIOnlyPlaceholderKey"}],
        "services": {"appinvite_service": {"other_platform_oauth_client": []}},
    }],
    "configuration_version": "1",
}
(root / "app/google-services.json").write_text(json.dumps(gs))
(root / "local.properties").write_text("curseforge.api_key=\n")

# Pin MinecraftAuth to avoid JitPack main-SNAPSHOT metadata timeouts.
versions = root / "gradle/libs.versions.toml"
vs = versions.read_text()
vs = vs.replace('minecraftauth = "main-SNAPSHOT"', 'minecraftauth = "ed320a0e882cbfd5d793e4fc9a92be0739b2bfb8"')
versions.write_text(vs)
