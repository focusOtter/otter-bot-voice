#!/usr/bin/env python3
"""Talk to the ElevenLabs agent exactly the way the ESP32 firmware does.

Streams a 16 kHz, 16-bit mono PCM clip as push-to-talk audio (100 ms chunks),
then streams digital silence like the firmware does when the button is up,
and saves the agent's reply audio to reply.wav.

  pip install websockets
  python3 tools/agent_smoke_test.py --agent-id agent_xxx --pcm question.pcm
"""
import argparse, asyncio, base64, json, time, wave

import websockets

CHUNK = 3200  # 100 ms of 16 kHz 16-bit mono


async def main(agent_id, pcm_path, out_path, wait_s):
    url = f"wss://api.elevenlabs.io/v1/convai/conversation?agent_id={agent_id}"
    speech = open(pcm_path, "rb").read()
    reply, turns, t_release = bytearray(), [], None
    async with websockets.connect(url, max_size=None) as ws:
        await ws.send(json.dumps({"type": "conversation_initiation_client_data"}))

        async def sender():
            nonlocal t_release
            await asyncio.sleep(6)  # let the greeting play, like a person would
            for i in range(0, len(speech), CHUNK):
                await ws.send(json.dumps({"user_audio_chunk": base64.b64encode(speech[i:i + CHUNK]).decode()}))
                await asyncio.sleep(0.1)
            t_release = time.time()
            silence = base64.b64encode(bytes(CHUNK)).decode()
            while True:
                await ws.send(json.dumps({"user_audio_chunk": silence}))
                await asyncio.sleep(0.1)

        task = asyncio.create_task(sender())
        first_reply_audio = None
        async def receiver():
            nonlocal first_reply_audio, reply
            async for raw in ws:
                m = json.loads(raw)
                t = m.get("type")
                if t == "ping":
                    await ws.send(json.dumps({"type": "pong", "event_id": m["ping_event"]["event_id"]}))
                elif t == "conversation_initiation_metadata":
                    md = m["conversation_initiation_metadata_event"]
                    print("connected:", md["conversation_id"], md["user_input_audio_format"], md["agent_output_audio_format"])
                elif t == "user_transcript":
                    print("you said:", m["user_transcription_event"]["user_transcript"])
                elif t == "agent_response":
                    txt = m["agent_response_event"]["agent_response"]
                    turns.append(txt)
                    print("devin:", txt)
                elif t == "audio" and t_release:
                    if first_reply_audio is None:
                        first_reply_audio = time.time() - t_release
                        print(f"reply audio started {first_reply_audio:.2f}s after button release")
                    reply += base64.b64decode(m["audio_event"]["audio_base_64"])
                if len(turns) >= 2 and t == "agent_response":
                    await asyncio.sleep(4)
                    break
        try:
            await asyncio.wait_for(receiver(), wait_s)
        except asyncio.TimeoutError:
            pass
        task.cancel()
        await asyncio.gather(task, return_exceptions=True)
    with wave.open(out_path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000); w.writeframes(reply)
    print(f"saved {len(reply) / 32000:.1f}s of reply audio to {out_path}")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--agent-id", required=True)
    p.add_argument("--pcm", required=True, help="raw pcm_16000 clip of you asking something")
    p.add_argument("--out", default="reply.wav")
    p.add_argument("--wait", type=float, default=40)
    a = p.parse_args()
    asyncio.run(main(a.agent_id, a.pcm, a.out, a.wait))
