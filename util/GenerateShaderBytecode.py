from pathlib import Path
import subprocess
import os
import sys
		
rootdir = sys.argv[1]

try:
	os.mkdir(f"{rootdir}/src/ShaderBytecode")
except OSError:
	pass

oldHeaders = os.listdir(f"{rootdir}/src/ShaderBytecode")
for filename in oldHeaders:
	os.remove(f"{rootdir}/src/ShaderBytecode/{filename}")

def isShader(filepath):
	shaderTypes = {".hlsl"}
	suffixes = Path(filepath).suffixes
	if len(suffixes) > 0:
		return suffixes[-1] in shaderTypes
	else:
		return False

p = Path(f"{rootdir}/src/Shaders")
shaderFiles = [shader for shader in p.iterdir() if isShader(shader)]
for shader in shaderFiles:
	varname = shader.name.replace(".", "_") + "_bytecode"
	outputHeader = f"{rootdir}/src/ShaderBytecode/{shader.name}.h"
	errcode = subprocess.call(f"glslangValidator -V --vn {varname} -o {outputHeader} {str(shader)} -e main")
	if errcode != 0:
		os.remove(outputHeader)
