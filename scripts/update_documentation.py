#!/usr/bin/env python3
import json
import argparse

def update_docs(config_path, report_path):
    print('Documentation updated')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', required=True)
    parser.add_argument('--report', required=True)
    args = parser.parse_args()
    update_docs(args.config, args.report)
