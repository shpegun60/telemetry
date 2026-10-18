#!/usr/bin/env python3
"""Qualify static RPC session ownership and offline Cortex-M7 dispatch roots.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from rpc_extension import run_suite

SOURCES = {
    'mailbox': ROOT / 'lib/resource/telemetry/v3/RpcMailbox.cpp',
    'protocol': ROOT / 'lib/resource/protocol/Protocol.cpp',
    'adapter': ROOT / 'lib/telemetry/model/Adapter.cpp',
    'abi': ROOT / 'lib/telemetry/abi/StructuredAbi.cpp',
}
NEGATIVES = {
    'RPC_SESSIONS_NEG_COPY': r'(?:deleted[^\n]*RpcSessionPool|RpcSessionPool[^\n]*deleted)',
    'RPC_SESSIONS_NEG_MOVE': r'(?:deleted[^\n]*RpcSessionPool|RpcSessionPool[^\n]*deleted)',
    'RPC_SESSIONS_NEG_COPY_ASSIGN': r'(?:deleted[^\n]*RpcSessionPool|RpcSessionPool[^\n]*deleted)',
    'RPC_SESSIONS_NEG_MOVE_ASSIGN': r'(?:deleted[^\n]*RpcSessionPool|RpcSessionPool[^\n]*deleted)',
    'RPC_SESSIONS_NEG_WRONG_CAPACITY': r'(?:no matching|no viable)[\s\S]*RpcSessionPool|RpcSessionPool[\s\S]*(?:no matching|no viable)',
    'RPC_SESSIONS_NEG_WRONG_BINDING': r'(?:no matching|no viable)[\s\S]*RpcSessionPool|RpcSessionPool[\s\S]*(?:no matching|no viable)',
    'RPC_SESSIONS_NEG_TEMPORARY_MAILBOX': r'(?:cannot bind|lvalue)[\s\S]*RpcMailbox|RpcMailbox[\s\S]*(?:cannot bind|lvalue)',
    'RPC_SESSIONS_NEG_ZERO_CAPACITY': r'RpcSessionPool requires at least one slot',
}
SPEC = dict(
    name='rpc-sessions', here=HERE, description=__doc__, prefix='rpc sessions',
    headers=('resource/telemetry/v3/RpcSessionPool.hpp',),
    sources=SOURCES, runtime_sources=SOURCES, negatives=NEGATIVES,
    example_sources={**SOURCES, 'client': ROOT / 'lib/resource/telemetry/v3/RpcClient.cpp'},
    roots=('rpc_sessions_acquire', 'rpc_sessions_handle', 'rpc_sessions_reply_accepted',
           'rpc_sessions_process', 'rpc_sessions_release', 'rpc_sessions_roots'),
    entry='rpc_sessions_roots', arm_define='RPC_SESSIONS_ARM_PROBE',
    frame_define='RPC_SESSIONS_FRAME_CONTROL', frame_function='rpc_sessions_frame_control',
    required_frames={'roots': 'rpc_sessions_process', 'mailbox': 'RpcMailbox::process'},
    required_symbols=('RpcMailbox::writeImpl', 'RpcMailbox::readImpl', 'RpcMailbox::processImpl'),
    odr_body='return sizeof(resource::telemetry::v3::RpcSessionPool<2>) +\n'
             '       sizeof(resource::telemetry::v3::RpcSessionHandle);',
    example=Path('examples/rpc_multi_session/main.cpp'), contract='doc/RpcSessionsV1.md',
    example_pattern=r'^RPC sessions example: (\d+) checks,',
    extra_runs=({'stem': 'Exhaustion', 'prefix': 'rpc sessions exhaustion'},),
    minima={'Check-0': 498, 'Check-32': 498, 'NoHeap-32': 498, 'Exhaustion': 28,
            'example': 247, 'aggregate': 1769},
    abi='header-only pool; existing RpcMailbox compiled ABI gate remains in tests/structured/rpc',
)

if __name__ == '__main__':
    run_suite(SPEC)
