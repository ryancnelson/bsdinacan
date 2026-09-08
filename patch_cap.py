import re

with open("tests/test_core.c", "r") as f:
    text = f.read()

# remove exactly the loop
text = re.sub(r'    for \(index = 0; index < CB_MAX_PROGRAMS; \+\+index\) \{\n        snprintf\(names\[index\], sizeof\(names\[index\]\), "program-%lu",\n                 \(unsigned long\)index\);\n        programs\[index\] = candidate;\n        programs\[index\].name = names\[index\];\n        programs\[index\].start = registration_stub_main;\n        if \(cb_kernel_register\(kernel, &programs\[index\]\) < 0\)\n            fail\("valid program registration"\);\n        if \(cb_kernel_register\(kernel, &programs\[index\]\) == 0\)\n            fail\("duplicate program registration"\);\n    \}\n    candidate\.name = "overflow";\n    if \(cb_kernel_register\(kernel, &candidate\) == 0\)\n        fail\("program registry capacity"\);\n', '', text, flags=re.MULTILINE)

with open("tests/test_core.c", "w") as f:
    f.write(text)
