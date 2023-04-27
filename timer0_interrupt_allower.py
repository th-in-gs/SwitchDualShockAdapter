Import("env")
import os

def preprocess_wiring_c_to_add_ISR_NOBLOCK(node):
    fileFrom = node.srcnode().get_abspath()
    
    dirTo = node.dir.get_abspath();
    fileTo = os.path.splitext(node.get_abspath())[0] + "-ISR_NOBLOCK.c"

    os.makedirs(dirTo, exist_ok=True)

    with open(fileFrom, 'r') as infile, open(fileTo, 'w') as outfile:
        for line in infile:
            outfile.write(line.replace("TIMER0_OVF_vect", "TIMER0_OVF_vect, ISR_NOBLOCK"))

    return env.File(fileTo)

env.AddBuildMiddleware(preprocess_wiring_c_to_add_ISR_NOBLOCK, "*/wiring.c")