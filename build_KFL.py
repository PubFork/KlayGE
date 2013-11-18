#!/usr/bin/env python
#-*- coding: ascii -*-

from __future__ import print_function
import sys
from blib_util import *

def build_KFL(compiler_info):
	for arch in compiler_info.arch_list:
		build_a_project("KFL", "KFL", compiler_info, arch)

if __name__ == "__main__":
	cfg = cfg_from_argv(sys.argv)
	ci = compiler_info(cfg.compiler, cfg.archs, cfg.cfg)

	print("Building KFL...")
	build_KFL(ci)
