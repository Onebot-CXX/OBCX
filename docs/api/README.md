# API documentation

- [OneBot11 通用群合并转发与 actor 策略边界](onebot11-forward.md)

English and Chinese Doxygen outputs are generated and audited together:

```bash
python3 scripts/generate_api_docs.py
```

The generated HTML is written to `docs/api/en/html` and
`docs/api/zh/html`. Generated files are intentionally ignored; CI rebuilds
both variants from the current `include/` tree and audits retired extension
identifiers. The generated set therefore includes both installed actor SDK
headers and process-only BotInstallation/component headers; `src/CMakeLists.txt`
is authoritative for the smaller installed SDK surface.
