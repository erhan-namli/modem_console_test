# Additional clean files

file(REMOVE_RECURSE
  "cert.pem.S"
  "esp-idf/esptool_py/flasher_args.json.in"
  "esp-idf/mbedtls/x509_crt_bundle"
  "flash_app_args"
  "flash_bootloader_args"
  "flasher_args.json"
  "ldgen_libraries"
  "ldgen_libraries.in"
  "littlefs_py_venv"
  "modem-console.map"
  "project_elf_src_esp32s3.c"
  "x509_crt_bundle.S"
)
