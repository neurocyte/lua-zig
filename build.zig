const std = @import("std");

pub fn build(b: *std.build.Builder) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const lib = b.addStaticLibrary(.{
        .name = "lua",
        .target = target,
        .optimize = optimize,
    });

    const flags = [_][]const u8{
        "-std=c99",

        switch (lib.target_info.target.os.tag) {
            .linux => "-DLUA_USE_LINUX",
            .macos => "-DLUA_USE_MACOSX",
            .windows => "-DLUA_USE_WINDOWS",
            else => "-DLUA_USE_POSIX",
        },

        // "-DLUA_USE_APICHECK",
    };

    lib.linkLibC();
    lib.addCSourceFiles(&.{
        "lapi.c",
        "lcode.c",
        "lctype.c",
        "ldebug.c",
        "ldo.c",
        "ldump.c",
        "lfunc.c",
        "lgc.c",
        "llex.c",
        "lmem.c",
        "lobject.c",
        "lopcodes.c",
        "lparser.c",
        "lstate.c",
        "lstring.c",
        "ltable.c",
        "ltm.c",
        "lundump.c",
        "lvm.c",
        "lzio.c",
        "lauxlib.c",
        "lbaselib.c",
        "lcorolib.c",
        "ldblib.c",
        "liolib.c",
        "lmathlib.c",
        "loadlib.c",
        "loslib.c",
        "lstrlib.c",
        "ltablib.c",
        "lutf8lib.c",
        "linit.c",
    }, &flags);
    lib.install();
    lib.installHeader("lapi.h", "lapi.h");
    lib.installHeader("lauxlib.h", "lauxlib.h");
    lib.installHeader("lcode.h", "lcode.h");
    lib.installHeader("lctype.h", "lctype.h");
    lib.installHeader("ldebug.h", "ldebug.h");
    lib.installHeader("ldo.h", "ldo.h");
    lib.installHeader("lfunc.h", "lfunc.h");
    lib.installHeader("lgc.h", "lgc.h");
    lib.installHeader("ljumptab.h", "ljumptab.h");
    lib.installHeader("llex.h", "llex.h");
    lib.installHeader("llimits.h", "llimits.h");
    lib.installHeader("lmem.h", "lmem.h");
    lib.installHeader("lobject.h", "lobject.h");
    lib.installHeader("lopcodes.h", "lopcodes.h");
    lib.installHeader("lopnames.h", "lopnames.h");
    lib.installHeader("lparser.h", "lparser.h");
    lib.installHeader("lprefix.h", "lprefix.h");
    lib.installHeader("lstate.h", "lstate.h");
    lib.installHeader("lstring.h", "lstring.h");
    lib.installHeader("ltable.h", "ltable.h");
    lib.installHeader("ltests.h", "ltests.h");
    lib.installHeader("ltm.h", "ltm.h");
    lib.installHeader("luaconf.h", "luaconf.h");
    lib.installHeader("lua.h", "lua.h");
    lib.installHeader("lualib.h", "lualib.h");
    lib.installHeader("lundump.h", "lundump.h");
    lib.installHeader("lvm.h", "lvm.h");
    lib.installHeader("lzio.h", "lzio.h");

    const exe = b.addExecutable(.{
        .name = "lua-cli",
        .target = target,
        .optimize = optimize,
    });
    exe.addCSourceFiles(&.{
        "lua.c",
    }, &flags);
    exe.linkLibrary(lib);
    exe.linkLibC();
    exe.install();
}
