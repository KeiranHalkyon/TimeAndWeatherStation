import winreg
# from typing import Union 
from fastapi import FastAPI
import uvicorn

#connecting to key in registry
access_registry = winreg.ConnectRegistry(None, winreg.HKEY_CURRENT_USER)
access_key = winreg.OpenKey(access_registry, r"Software\HWiNFO64\VSB")
app = FastAPI()

@app.get("/data")
def read_data():
    memLoad = float(winreg.QueryValueEx(access_key,"ValueRaw0")[0])
    cpuCoreUsage = float(winreg.QueryValueEx(access_key,"ValueRaw1")[0])
    cpuTemp = float(winreg.QueryValueEx(access_key,"ValueRaw2")[0])
    cpuThrottle = 1 if winreg.QueryValueEx(access_key,"ValueRaw3")[0] == "Yes" else 0
    gpuTemp = float(winreg.QueryValueEx(access_key,"ValueRaw4")[0])
    gpuCoreLoad = float(winreg.QueryValueEx(access_key,"ValueRaw5")[0])
    framerate = float(winreg.QueryValueEx(access_key,"ValueRaw7")[0])
    frametime = float(winreg.QueryValueEx(access_key,"ValueRaw8")[0])
    wifiDl = float(winreg.QueryValueEx(access_key,"ValueRaw9")[0])
    wifiUp = float(winreg.QueryValueEx(access_key,"ValueRaw10")[0])
    ethDl = float(winreg.QueryValueEx(access_key,"ValueRaw11")[0])
    ethUp = float(winreg.QueryValueEx(access_key,"ValueRaw12")[0])
    
    jsonOb = {'cpuTemp': cpuTemp,
            'cpuLoad': cpuCoreUsage,
            'cpuThrottle': cpuThrottle,
            'downloadRate': wifiDl+ethDl,
            'uploadRate': wifiUp+ethUp,
            'gpuLoad': gpuCoreLoad,
            'gpuTemp': gpuTemp,
            'memory': memLoad,
            'framerate': framerate,
            'frametime': frametime}
    return jsonOb

if __name__ == "__main__":
    uvicorn.run(app, host='0.0.0.0', port=4000)

#registry parameter list
#0: physical memory load
#1: cpu core usage
#2: cpu package temp
#3: cpu throttling
#4: gpu temp
#5: gpu core load
#6: charge level
#7: framerate presented
#8: frame time
#9: wifi dl rate
#10: wifi up rate
#11: eth dl rate
#12: eth up rate

# while(True):
#     memLoad = float(winreg.QueryValueEx(access_key,"ValueRaw0")[0])
#     cpuCoreUsage = float(winreg.QueryValueEx(access_key,"ValueRaw1")[0])
#     cpuTemp = float(winreg.QueryValueEx(access_key,"ValueRaw2")[0])
#     cpuThrottle = 1 if winreg.QueryValueEx(access_key,"ValueRaw3")[0] == "Yes" else 0
#     gpuTemp = float(winreg.QueryValueEx(access_key,"ValueRaw4")[0])
#     gpuCoreLoad = float(winreg.QueryValueEx(access_key,"ValueRaw5")[0])
#     # chargeLvl = float(winreg.QueryValueEx(access_key,"ValueRaw6")[0])
#     # framerate = float(winreg.QueryValueEx(access_key,"ValueRaw7")[0])
#     # frametime = float(winreg.QueryValueEx(access_key,"ValueRaw8")[0])
#     wifiDl = float(winreg.QueryValueEx(access_key,"ValueRaw9")[0])
#     wifiUp = float(winreg.QueryValueEx(access_key,"ValueRaw10")[0])
#     ethDl = float(winreg.QueryValueEx(access_key,"ValueRaw11")[0])
#     ethUp = float(winreg.QueryValueEx(access_key,"ValueRaw12")[0])

#     # url = "http://esp8266.local/data"
#     # jsonOb = {'cpuTemp': cpuTemp,
#     #         'cpuLoad': cpuCoreUsage,
#     #         'cpuThrottle': cpuThrottle,
#     #         'battery': chargeLvl,
#     #         'downloadRate': wifiDl+ethDl,
#     #         'uploadRate': wifiUp+ethUp,
#     #         'fps': framerate,
#     #         'frametime': frametime,
#     #         'gpuLoad': gpuCoreLoad,
#     #         'gpuTemp': gpuTemp,
#     #         'memory': memLoad}

#     url = "http://esp8266.local/data?"
#     url += "&ct="+str(cpuTemp)
#     url += "&cl="+str(cpuCoreUsage)
#     url += "&cth="+str(cpuThrottle)
#     url += "&dlr="+str(wifiDl+ethDl)
#     url += "&upr="+str(wifiUp+ethUp)
#     url += "&gl="+str(gpuCoreLoad)
#     url += "&gt="+str(gpuTemp)
#     url += "&mem="+str(memLoad)
#     # print(url)
#     try:
#         # req = requests.post(url, json=jsonOb)
#         req = requests.post(url)
#         print(req.text)
#     except (ConnectionError, TimeoutError):
#         print("Could not connect")

#     time.sleep(10)

