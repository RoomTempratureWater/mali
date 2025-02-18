from fastapi import FastAPI, Request, UploadFile, BackgroundTasks, File, HTTPException, Depends, Security
from fastapi.templating import Jinja2Templates
import requests
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from fastapi.middleware.cors import CORSMiddleware
import json
import httpx
from fastapi.responses import StreamingResponse
import os
import uvicorn
from typing import Annotated
import datetime
#from .db import SUPAB
from pydantic import BaseModel, HttpUrl
app = FastAPI()
#supb = SUPAB()
#supb.client_init()
templates = Jinja2Templates("app/templates")
import requests
import asyncio

app.add_middleware(
    CORSMiddleware,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

security = HTTPBearer()
async def verify_google_token(credentials: HTTPAuthorizationCredentials = Security(security)):
    """Validate Google OAuth2 Token using Google's token info API"""
    token = credentials.credentials
    async with httpx.AsyncClient() as client:
        response = await client.get(f"https://www.googleapis.com/oauth2/v3/tokeninfo",headers={"Authorization":"Bearer"+token})
    
    if response.status_code != 200:
        raise HTTPException(status_code=401, detail="Invalid Google token")
    
    return response.json()  # Returns user info from Google
@app.get('/')
async def name(request: Request):
    err = ""
    return templates.TemplateResponse('login.html', {"request": request, 'error': err})

@app.get('/login')
async def page2(request: Request):
    err = ''
    if 'error' in request.query_params:
        err = request.query_params['error']
    return templates.TemplateResponse('login.html', {"request": request, 'error': err})
@app.get('/getinfo/{token}')
async def getinfo(request:Request, token):
    res = requests.get("https://www.googleapis.com/oauth2/v3/userinfo",
                             headers={"Authorization":"Bearer"+token}) 
    dumped_res = res.text
    print(res.text)
    return dumped_res
@app.get('/me')
async def me(request:Request):
    user = {"name": "Jonathan"}
    return templates.TemplateResponse('meprev.html', {"request":request, "user": user})


webhooks = {}

user_devices = {}  # { "user@example.com": ["esp32_01", "esp32_02"] }
message_queues = {}  # { "esp32_01": asyncio.Queue() }
device_data_store = {}  # Store latest ESP32 data

class DeviceData(BaseModel):
    user_email: str
    device_id: str

class Command(BaseModel):
    user_email: str
    device_id: str
    command: str  # Example: "led_on", "led_off", "send_data"

class SensorData(BaseModel):
    user_email: str
    device_id: str
    temperature: float
    humidity: float

@app.get('/me/devices')
async def name(request: Request, user_info: dict = Depends(verify_google_token)):
    user_email = user_info["email"]
    print("user", user_email)
    devices = user_devices[user_email]
    return templates.TemplateResponse("data.html", {"request": request, "devices": devices, "email":user_email})

@app.post("/esp32/register")
async def register_esp32(data: DeviceData):
    """Register an ESP32 to a specific user."""
    if data.user_email not in user_devices:
        user_devices[data.user_email] = []
    
    if data.device_id not in user_devices[data.user_email]:
        user_devices[data.user_email].append(data.device_id)
    
    if data.device_id not in message_queues:
        message_queues[data.device_id] = asyncio.Queue()

    return {"status": f"ESP32 {data.device_id} linked to {data.user_email}"}

@app.get("/esp32/updates/{device_id}")
async def sse_updates(device_id: str):
    """ESP32 listens for commands from the server (SSE)."""

    # if user_email not in user_devices or device_id not in user_devices[user_email]:
    #     raise HTTPException(status_code=403, detail="Not authorized for this device")
    if device_id not in message_queues:
        raise HTTPException(status_code=404, detail="Device not connected")
    async def event_generator():
        while True:
            message = await message_queues[device_id].get()
            yield f"data: {message}\n\n"

    return StreamingResponse(event_generator(), media_type="text/event-stream")

@app.post("/send_command")
async def send_command(data: Command, user_info: dict = Depends(verify_google_token)):
    """Send a command to an ESP32 (Only if user owns the device)."""
    print(message_queues)
    did = data.device_id.replace("&gt;", "")
    print(did)
    if did in message_queues:
        await message_queues[did].put('{"command": "' + data.command + '"}')
        return {"status": f"Command '{data.command}' sent to {did}"}
    
    raise HTTPException(status_code=404, detail="Device not connected")

@app.post("/esp32/send_data")
async def receive_data(data: SensorData):
    """ESP32 sends data to the server in response to a request."""
    # Store latest data from ESP32
    print(data)
    device_data_store[data.device_id] = {"temperature": data.temperature, "humidity": data.humidity}
    
    return {"status": "Data received successfully"}

@app.post("/request_data")
async def request_data(data: DeviceData):
    """Server requests data from ESP32 (ESP32 will respond via /send_data)."""
    if data.device_id not in user_devices.get(data.user_email, []):
        raise HTTPException(status_code=403, detail="Not authorized for this device")

    if data.device_id in message_queues:
        await message_queues[data.device_id].put('{"command": "send_data"}')  # Ask ESP32 to send its data
        return {"status": "Data request sent to ESP32"}
    
    raise HTTPException(status_code=404, detail="Device not connected")

@app.get("/get_latest_data/{device_id}")
async def get_latest_data(device_id: str, user_info: dict = Depends(verify_google_token)):
    """Retrieve the latest data from ESP32 after requesting it."""
    print(device_data_store)
    user_email = user_info["email"]
    if device_id not in user_devices.get(user_email, []):
        raise HTTPException(status_code=403, detail="Not authorized for this device")

    return device_data_store.get(device_id, {"status": "No data received yet"})
# @app.get("/get_devices")
# async def get_devices(user_id: dict = Depends(verify_google_token)):
#     if user_id in webhooks:
#         return get_data(user_id)

@app.get("/list_webhooks")
async def list_webhooks():
    """Lists all registered webhooks (for debugging purposes)."""
    return webhooks



# if __name__ == "__main__":
#    uvicorn.run("app:app", port=8000, reload=True)