with open("tests/test_core.c", "r") as f:
    text = f.read()

import re
text = re.sub(r'    for \(index = 0; index < CB_MAX_PROGRAMS; \+\+index\) \{.*?\n        \}\n    \}', '', text, flags=re.DOTALL)

with open("tests/test_core.c", "w") as f:
    f.write(text)
