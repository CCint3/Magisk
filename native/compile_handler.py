import json

# 读取 JSON 文件
with open("native/compile_commands.merge.json", "r") as json_file:
    data = json.load(json_file)

ary = []

# 遍历 JSON 对象的键值对
for item in data:
    if item["output"].find("/armeabi-v7a") != -1:
        continue
    if item["output"].find("/x86") != -1:
        continue
    
    ary.append(item)
    # print(f"{item},")

with open("native/compile_commands.json", "w") as fd:
    fd.write(json.dumps(ary))
