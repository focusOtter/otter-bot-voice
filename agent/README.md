# The "Devin" ElevenLabs agent

`devin-agent.json` is the exact config the bench test's agent was created with
(Flash v2 TTS, Gemini 2.5 Flash, 16 kHz PCM in and out, 5-minute calls, 100 calls/day cap).

Recreate it on another ElevenLabs account:

```bash
curl -X POST https://api.elevenlabs.io/v1/convai/agents/create \
  -H "xi-api-key: $ELEVENLABS_API_KEY" -H "Content-Type: application/json" \
  -d @agent/devin-agent.json
```

The response's `agent_id` goes in `firmware/otter_voice/secrets.h`. Tweak the persona in
ElevenLabs > Agents > Devin (otter bot bench test) > Agent > System prompt; no reflash needed.

The agent is public (no signed URL), so anyone holding the agent ID can call it on your credits.
That's why the ID lives in the gitignored `secrets.h` and the agent has a daily call cap.
