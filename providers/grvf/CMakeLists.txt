rdma_shared_provider(grvf libgrvf.map
  1 1.0.${PACKAGE_VERSION}
  grvf.c
  grvf_post.c
)

publish_headers(infiniband
  grvfdv.h
)

rdma_pkg_config("grvf" "libibverbs" "${CMAKE_THREAD_LIBS_INIT}")
