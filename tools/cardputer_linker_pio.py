"""PlatformIO post-framework hook: only the Cardputer environment uses this."""
Import("env")
from pathlib import Path
import sys

root = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(root / "tools"))
from cardputer_linker import generate, verify, replace_link_flags, require, POLICY

require(env.subst("$PIOENV") == "cardputer-adv-lora1262", "hook may only run for Cardputer")
platform = env.PioPlatform()
sdk = Path(platform.get_package_dir("framework-arduinoespressif32-libs")) / "esp32s3"
toolchain = Path(platform.get_package_dir("toolchain-xtensa-esp-elf")) / "bin"
objdump = toolchain / "xtensa-esp32s3-elf-objdump"
if not objdump.exists():
    objdump = objdump.with_suffix(".exe")
require(objdump.exists(), "pinned objdump unavailable")
output = Path(env.subst("$BUILD_DIR")) / "cardputer_linker"
generate(sdk, objdump, output)
env.Replace(LINKFLAGS=replace_link_flags(env["LINKFLAGS"], output / "sections.ld"))
elf = env.subst("$BUILD_DIR/${PROGNAME}.elf")
env.Depends(elf, [str(output / "sections.ld"), str(root / "tools/cardputer_linker.py"), str(root / "tools/cardputer_linker_pio.py"), str(POLICY / "provenance.json"), str(POLICY / "freertos.lf"), str(POLICY / "esp_ringbuf.lf")])

def verify_cardputer_linker(source, target, env):
    elf_path = Path(str(target[0]))
    result = verify(output, elf_path.with_suffix(".map"), elf_path)
    print("Cardputer linker placement verified: %d moved input-section bytes; ISR/cache-off code retained" % result["moved_bytes"])

env.AddPostAction(elf, verify_cardputer_linker)
