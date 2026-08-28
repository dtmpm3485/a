const std = @import("std");

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const exe_dir = try std.fs.selfExeDirPathAlloc(allocator);
    defer allocator.free(exe_dir);

    const required = [_][]const u8{
        "PrivacyPic.exe",
        "privacypic_core.dll",
        "privacypic_guard.dll",
        "integrity.json",
        "integrity.sig",
    };

    for (required) |name| {
        const full = try std.fs.path.join(allocator, &.{ exe_dir, name });
        defer allocator.free(full);
        var file = std.fs.openFileAbsolute(full, .{}) catch {
            std.debug.print("PrivacyPic: required file missing: {s}\n", .{name});
            return;
        };
        file.close();
    }

    const app_path = try std.fs.path.join(allocator, &.{ exe_dir, "PrivacyPic.exe" });
    defer allocator.free(app_path);

    var env_map = try std.process.getEnvMap(allocator);
    defer env_map.deinit();
    try env_map.put("PRIVACYPIC_LAUNCH_TOKEN", "PP2-PRIVACYPIC-LAUNCHER-V2");

    const argv = [_][]const u8{app_path};
    var child = std.process.Child.init(&argv, allocator);
    child.cwd = exe_dir;
    child.env_map = &env_map;

    _ = try child.spawnAndWait();
}
