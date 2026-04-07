import sys

filepath = r'c:\GitHub\q_mini_wasm_v2\q_mini_wasm_v2\core\flash_cim\flash_cim.hpp'

with open(filepath, 'r') as f:
    content = f.read()

if '3D_XPOINT' in content:
    content = content.replace('3D_XPOINT', 'THREE_D_XPOINT')
    with open(filepath, 'w') as f:
        f.write(content)
    print('Fixed 3D_XPOINT -> THREE_D_XPOINT')
else:
    print('3D_XPOINT not found in file')
