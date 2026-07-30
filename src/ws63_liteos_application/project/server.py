from flask import Flask,request


app=Flask(__name__)


@app.route('/scan',methods=['POST'])
def scan():

    data=request.json

    print(data)

    return {
        "result":"ok"
    }


app.run(
    host="0.0.0.0",
    port=5000
)