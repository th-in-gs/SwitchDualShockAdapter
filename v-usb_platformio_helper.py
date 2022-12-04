Import("env")

# V-USB has a '.asm' file in the usbdrv folder that PlatformIO will try to 
# compile - but it's really just for the "IAR compiler/assembler system" and 
# will just cause errors. We'll make it be skipped.
def skip_file(node):
    return None
env.AddBuildMiddleware(skip_file, "*/lib/v-usb/*.asm")

# Make sure V-USB can find its config file, which we've placed inside the 
# top-level include folder.
def add_usbdrv_include(node):
    return env.Object(
        node,
        CFLAGS=env["CFLAGS"] + ["-Iinclude"],
        ASFLAGS=env["ASFLAGS"] + ["-Iinclude"],
    )
env.AddBuildMiddleware(add_usbdrv_include, "*/lib/v-usb/*.[chS]")