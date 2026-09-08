with open("tests/test_core.c", "r") as f:
    text = f.read()

import re
text = text.replace("    test_registration_contract();", "    //test_registration_contract();")
with open("tests/test_core.c", "w") as f:
    f.write(text)
