#!/usr/bin/env python3
" Flask API Server for Kanban Agent Integration
import asyncio
from flask import Flask, request, jsonify
from flask_cors import CORS
import threading, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from kanban_review_fix_agent import KanbanReviewFixAgent, AgentConfig
app = Flask(__name__)
CORS(app)
kanban_agent = None
agent_lock = threading.Lock()
def get_kanban_agent():
    global kanban_agent
    with agent_lock:
        if kanban_agent is None:
            config = AgentConfig(name=KanbanReviewFixAgent, description=Fix stuck cards, system_prompt=Fix stuck cards, tools=[scan,fix,report])
 kanban_agent = KanbanReviewFixAgent(config)
 return kanban_agent
@app.route(/api/v1/health)
def health(): return jsonify({status:healthy})
@app.route(/api/v1/kanban/scan, methods=[POST])
def scan():
 try:
 agent = get_kanban_agent()
 loop = asyncio.new_event_loop()
 asyncio.set_event_loop(loop)
 async def run():
 await agent.initialize()
 r = await agent.execute_task({action:scan})
 await agent.shutdown()
 return r
 r = loop.run_until_complete(run())
 loop.close()
 return jsonify({success:r.success,data:r.data,errors:r.errors or []})
 except Exception as e: return jsonify({success:False,error:str(e)}),500
@app.route(/api/v1/kanban/fix, methods=[POST])
def fix():
 try:
 agent = get_kanban_agent()
 data = request.get_json() or {}
 loop = asyncio.new_event_loop()
 asyncio.set_event_loop(loop)
 async def run():
 await agent.initialize()
 task = {action:fix}
 if data.get(card_id): task[card_id]=data[card_id]
 r = await agent.execute_task(task)
 await agent.shutdown()
 return r
 r = loop.run_until_complete(run())
 loop.close()
 return jsonify({success:r.success,data:r.data,errors:r.errors or []})
 except Exception as e: return jsonify({success:False,error:str(e)}),500
@app.route(/api/v1/kanban/report, methods=[POST])
def report():
 try:
 agent = get_kanban_agent()
 loop = asyncio.new_event_loop()
 asyncio.set_event_loop(loop)
 async def run():
 await agent.initialize()
 r = await agent.execute_task({action:report})
 await agent.shutdown()
 return r
 r = loop.run_until_complete(run())
 loop.close()
 return jsonify({success:r.success,data:r.data,errors:r.errors or []})
 except Exception as e: return jsonify({success:False,error:str(e)}),500
if __name__==__main__:
 print(Starting Kanban Agent API Server on http://localhost:5000)
 app.run(host=0.0.0.0,port=5000,debug=False)
