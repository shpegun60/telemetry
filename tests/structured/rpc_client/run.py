#!/usr/bin/env python3
"""Qualify typed asynchronous RpcClient host behavior and offline Cortex-M7 roots.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from rpc_extension import run_suite

CLIENT = ROOT / 'lib/client/rpc/RpcClient.cpp'
SOURCES = {'client': CLIENT}
RUNTIME_SOURCES = {
    **SOURCES,
    'mailbox': ROOT / 'lib/integration/telemetry/v3/RpcMailbox.cpp',
    'protocol': ROOT / 'lib/resource/protocol/Protocol.cpp',
    'adapter': ROOT / 'lib/telemetry/model/Adapter.cpp',
    'abi': ROOT / 'lib/telemetry/abi/StructuredAbi.cpp',
}
NEGATIVES = {
    'RPC_CLIENT_NEG_COPY': r'(?:deleted[^\n]*RpcClient|RpcClient[^\n]*deleted)',
    'RPC_CLIENT_NEG_MOVE': r'(?:deleted[^\n]*RpcClient|RpcClient[^\n]*deleted)',
    'RPC_CLIENT_NEG_UNTYPED': r'(?:no matching|no viable)[\s\S]*(?:beginReadField|FieldEndpoint)',
    'RPC_CLIENT_NEG_REQUEST_TYPE': r'(?:deduced conflicting|conflicting types|deduced[^\n]*conflict)',
    'RPC_CLIENT_NEG_FIELD_VOID': r'RPC Field value cannot be void',
    'RPC_CLIENT_NEG_CONST_TYPE': r'RPC endpoint types must be unqualified values',
    'RPC_CLIENT_NEG_UNSUPPORTED': r'Unsupported structured wire type',
    'RPC_CLIENT_NEG_DECODE_CONST': r'Decode output must be a mutable non-volatile object',
    'RPC_CLIENT_NEG_COMMAND_SCALAR': r'RPC Command request must be an aggregate struct or void',
    'RPC_CLIENT_NEG_SERVICE_SCALAR_REQUEST': r'RPC Service request must be an aggregate struct or void',
    'RPC_CLIENT_NEG_SERVICE_SCALAR_RESPONSE': r'RPC Service response must be an aggregate struct or void',
}
SPEC = dict(
    name='rpc-client', here=HERE, description=__doc__, prefix='rpc_client',
    headers=('client/rpc/RpcClient.hpp', 'wire/RpcWire.hpp'),
    sources=SOURCES, runtime_sources=RUNTIME_SOURCES, negatives=NEGATIVES,
    roots=('rpc_client_begin', 'rpc_client_packet', 'rpc_client_accepted', 'rpc_client_reply',
           'rpc_client_process', 'rpc_client_decode', 'rpc_client_reset', 'rpc_client_roots'),
    entry='rpc_client_roots', arm_define='RPC_CLIENT_ARM_PROBE',
    frame_define='RPC_CLIENT_FRAME_CONTROL', frame_function='rpc_client_frame_control',
    required_frames={'client': 'RpcClient::nextPacket'},
    required_symbols=('RpcClient::nextPacket', 'RpcClient::onReply', 'RpcClient::onSendAccepted'),
    odr_return_type='const void*',
    odr_body='return telemetry::client::rpc::client_detail::typeToken<unsigned>();',
    example=Path('examples/rpc_client/main.cpp'), contract='doc/RpcClientV1.md',
    example_pattern=r'^rpc_client checks=(\d+) failures=0\s*$',
    extra_runs=({'stem': 'Exhaustion', 'prefix': 'rpc_client', 'sources': {}},),
    minima={'Check-0': 1730, 'Check-32': 1730, 'NoHeap-32': 1730, 'Exhaustion': 16,
            'example': 45, 'aggregate': 5251},
    abi='none; RpcClient has no compiled structured-layout dependency',
)

if __name__ == '__main__':
    run_suite(SPEC)
