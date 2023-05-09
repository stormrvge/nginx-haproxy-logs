core.register_action("log_performance_metrics", { "http-req" }, function(txn)
    local client_ip = txn.sf:src()
    local client_port = txn.sf:src_port()
    local server_ip = txn.sf:dst()
    local server_port = txn.sf:dst_port()
    local request_time = txn.sf:req_time_ms()
    local response_time = txn.sf:resp_time_ms()
    local duration = txn.sf:session_age_ms()
    local response_code = txn.sf:status()

    local log_entry = string.format(
        "%s:%d, %d, %d, %d, %d\n",
        server_ip, server_port, request_time, response_time, duration, response_code
    )

    local file = io.open("/path/to/logfile.log", "a")
    file:write(log_entry)
    file:close()
end)