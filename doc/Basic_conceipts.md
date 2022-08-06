# Basic

## Flowchart for sending data

JS programs -> LMDB(js_cpp) -> comm_adapter_bs -> UDP Multicast

## Flowchart for receive data

UDP Multicast -> comm_adapter_bs -> LMDB(cpp_js) -> JS programs
