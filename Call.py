import requests
import json

url = "https://open-api.bser.io/v1/user/nickname?query=*USER_NICKNAME*"
# a basic Get UID endpoint is used.
header = { "x-api-key" : "*YOUR_API_KEY*"}
# the header must include the x-api-key.
res = requests.get(url, headers=header)