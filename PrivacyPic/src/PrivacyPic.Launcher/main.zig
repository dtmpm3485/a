const std = @import("std");

const APP_SHA256 = "__APP_SHA256__";
const GUARD_SHA256 = "__GUARD_SHA256__";
const CORE_SHA256 = "__CORE_SHA256__";

fn hashMatches(path: []const u8, expected: []const u8) !bool {
    var file = try std.fs.openFileAbsolute(path, .{});
    defer file.close();

    var hasher = std.crypto.hash.sha2.Sha256.init(.{});
    var buf: [64 * 1024]u8 = undefined;
    while (true) {
        const n = try file.read(&buf);
        if (n == 0) break;
        hasher.update(buf[0..n]);
    }

    var digest: [32]u8 = undefined;
    hasher.final(&digest);
    const hex = std.fmt.bytesToHex(digest, .lower);
    return std.mem.eql(u8, hex[0..], expected);
}

pub fn main() !void {
    const allocator = std.heap.page_allocator;
    const dir = try std.fs.selfExeDirPathAlloc(allocator);
    defer allocator.free(dir);

    const app = try std.fs.path.join(allocator, &.{ dir, "PrivacyPicApp.exe" });
    defer allocator.free(app);
    const guard = try std.fs.path.join(allocator, &.{ dir, "PrivacyPicGuard.dll" });
    defer allocator.free(guard);
    const core = try std.fs.path.join(allocator, &.{ dir, "privacypic_core.dll" });
    defer allocator.free(core);

    if (!(try hashMatches(app, APP_SHA256))) return error.AppIntegrity;
    if (!(try hashMatches(guard, GUARD_SHA256))) return error.GuardIntegrity;
    if (!(try hashMatches(core, CORE_SHA256))) return error.CoreIntegrity;

    var child = std.process.Child.init(&.{ app, "--pp-launch-v2" }, allocator);
    try child.spawn();
    _ = try child.wait();
}
