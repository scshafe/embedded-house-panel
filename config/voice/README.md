# Fake Recognition Policy

The template is fixture configuration only. The canonical builder in
`shared/spoken_command_catalog` validates every decision field, binds a generated
backend artifact and `deterministic-fake-v1/no-acoustic-model` identity, and
computes the recognition policy digest. The broker recomputes and compares it.
Changing VAD, endpointing, confidence/margin, silence, cooldown, debounce, or
postprocessing changes identity and invalidates any prior qualification.

There is no production audio configuration, selected recognizer, or acoustic
claim. Bound artifacts are generated under ignored `.build/voice`; no private
audio, credentials, or transcripts belong here.
