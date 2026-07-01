import os
import glob
import re

directory = "/home/lisabeth/qgroundcontrol/custom/src/core"
output_file = "/home/lisabeth/qgroundcontrol/custom/src/core/all_evaluates.txt"

cpp_files = glob.glob(os.path.join(directory, "*Check.cpp"))

with open(output_file, 'w') as out_f:
    for file in sorted(cpp_files):
        with open(file, 'r') as in_f:
            content = in_f.read()
            
        # Match void ClassName::evaluate() { ... }
        # Simple regex: find 'void \w+::evaluate()' and the matching block.
        match = re.search(r'void\s+\w+::evaluate\(\)[^{]*{', content)
        if match:
            start_index = match.end() - 1
            brace_count = 0
            end_index = -1
            for i in range(start_index, len(content)):
                if content[i] == '{':
                    brace_count += 1
                elif content[i] == '}':
                    brace_count -= 1
                    if brace_count == 0:
                        end_index = i + 1
                        break
            
            if end_index != -1:
                func_body = content[match.start():end_index]
                out_f.write(f"=== {os.path.basename(file)} ===\n")
                out_f.write(func_body + "\n\n")
            else:
                out_f.write(f"=== {os.path.basename(file)} ===\n")
                out_f.write("COULD NOT PARSE evaluate()\n\n")
        else:
            out_f.write(f"=== {os.path.basename(file)} ===\n")
            out_f.write("NO evaluate() METHOD FOUND\n\n")

print(f"Done. Wrote to {output_file}")
