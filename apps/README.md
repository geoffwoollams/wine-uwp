# Per-app settings

`run-app.sh` sources `apps/<package name>.conf` (the `Identity Name` in the app's `AppxManifest.xml`) before it starts the app, if the file exists. Use it for environment variables an app needs, such as `WINE_APP_STORE_ID` or `WINEDLLOVERRIDES` additions. Nothing here is required for an app to start.
