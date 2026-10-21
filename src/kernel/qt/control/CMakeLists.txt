# ============================================================================
# Kernel / Control（控制层：请求如何抵达内核）
# ----------------------------------------------------------------------------
#   remote/  Remote Connection Interface(API)：webui/ pairing/ napcat/ 三种远程传输
#   local/   Local Direct Connection：进程内直连，同时是内核的唯一装配点（Kernel）
# ============================================================================

add_subdirectory(remote)
add_subdirectory(local)
