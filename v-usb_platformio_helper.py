Import("env")

# V-USB has a '.asm' file in the usbdrv folder that PlatformIO will try to 
# compile, but it's really just for "The IAR compiler/assembler system".
# We'll make it be skipped.
def skip_file(node):
    return None
env.AddBuildMiddleware(skip_file, "*/lib/usbdrv/*.asm")

# Make sure V-USB can find its config file, which we've placed inside the 
# top-level include folder.
def add_usbdrv_include(node):
    return env.Object(
        node,
        CFLAGS=env["CFLAGS"] + ["-Iinclude"],
        ASFLAGS=env["ASFLAGS"] + ["-Iinclude"],
    )
env.AddBuildMiddleware(add_usbdrv_include, "*/lib/usbdrv/*.[chS]")
