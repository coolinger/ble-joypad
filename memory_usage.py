Import("env")
import os

def print_memory_usage(source, target, env):
    map_file = str(target[0]).replace(".elf", ".map")
    if os.path.exists(map_file):
        with open(map_file, 'r') as f:
            content = f.read()
            
        # Find memory usage section
        if "Memory Configuration" in content:
            print("\n=== Detailed Memory Usage ===")
            lines = content.split('\n')
            in_section = False
            for line in lines:
                if "Memory Configuration" in line:
                    in_section = True
                if in_section and ('dram0' in line or 'iram0' in line or '.bss' in line or '.data' in line):
                    print(line)
                    
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", print_memory_usage)