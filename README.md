### NGINX Module
You have to compile Nginx with this module. Config made on static module, but you can rewrite config on dynamic, to add dynamically module in nginx.


### HAProxy
1. Include the Lua script in your haproxy.cfg:
  ```
    global
    lua-load /path/to/performance_metrics.lua
  ```
2. Use the log_performance_metrics action in a frontend or backend section of your haproxy.cfg
   ```
    frontend http
    ...
    http-request lua.log_performance_metrics
    ...
   ```
