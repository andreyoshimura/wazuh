# Copyright (C) 2015-2021, Wazuh Inc.
# Created by Wazuh, Inc. <info@wazuh.com>.
# This program is a free software; you can redistribute it and/or modify it under the terms of GPLv2

import copy
from unittest.mock import patch

import pytest

from api import configuration, api_exception
from wazuh.core import common

custom_api_configuration = {
    "host": "0.0.0.0",
    "port": 55000,
    "use_only_authd": False,
    "drop_privileges": True,
    "experimental_features": False,
    "https": {
        "enabled": True,
        "key": "api/configuration/ssl/server.key",
        "cert": "api/configuration/ssl/server.crt",
        "use_ca": False,
        "ca": "api/configuration/ssl/ca.crt",
        "ssl_cipher": "TLSv1.2"
    },
    "logs": {
        "level": "info",
        "path": "logs/api.log"
    },
    "cors": {
        "enabled": False,
        "source_route": "*",
        "expose_headers": "*",
        "allow_headers": "*",
        "allow_credentials": False,
    },
    "cache": {
        "enabled": True,
        "time": 0.750
    },
    "access": {
        "max_login_attempts": 50,
        "block_time": 300,
        "max_request_per_minute": 300
    },
    "remote_commands": {
        "localfile": {
            "enabled": True,
            "exceptions": []
        },
        "wodle_command": {
            "enabled": True,
            "exceptions": []
        }
    }
}

custom_incomplete_configuration = {
    "logs": {
        "level": "DEBUG"
    },
    "use_only_authd": True
}


def check_config_values(config, read_config, default_config):
    for k in default_config.keys() - read_config.keys():
        if isinstance(default_config[k], str):
            assert config[k].lower() == default_config[k].lower()
        elif isinstance(default_config[k], dict):
            check_config_values(config[k], read_config.get(k, {}), default_config[k])
        else:
            assert config[k] == default_config[k]


@pytest.mark.parametrize('read_config', [
    {},
    configuration.default_api_configuration,
    custom_api_configuration,
    custom_incomplete_configuration
])
@patch('os.path.exists', return_value=True)
@patch('builtins.open')
def test_read_configuration(mock_open, mock_exists, read_config):
    """ Tests reading different API configurations."""
    with patch('api.configuration.yaml.safe_load') as m:
        m.return_value = copy.deepcopy(read_config)
        config = configuration.read_yaml_config()
        for section, subsection in [('logs', 'path'), ('https', 'key'), ('https', 'cert'), ('https', 'ca')]:
            config[section][subsection] = config[section][subsection].replace(common.wazuh_path+'/', '')

        check_config_values(config, {}, read_config)

        # values not present in the read user configuration will be filled with default values
        check_config_values(config, read_config, configuration.default_api_configuration)


@pytest.mark.parametrize('config', [
    {'invalid_key': 'value'},
    {'host': 1234},
    {'port': 'invalid_type'},
    {'use_only_authd': 'invalid_type'},
    {'drop_privileges': 'invalid_type'},
    {'experimental_features': 'invalid_type'},
    {'https': {'enabled': 'invalid_type'}},
    {'https': {'key': 12345}},
    {'https': {'cert': 12345}},
    {'https': {'use_ca': 12345}},
    {'https': {'ca': 12345}},
    {'https': {'ssl_cipher': 12345}},
    {'https': {'invalid_subkey': 'value'}},
    {'logs': {'level': 12345}},
    {'logs': {'path': 12345}},
    {'logs': {'invalid_subkey': 'value'}},
    {'cors': {'enabled': 'invalid_type'}},
    {'cors': {'source_route': 12345}},
    {'cors': {'expose_headers': 12345}},
    {'cors': {'allow_headers': 12345}},
    {'cors': {'allow_credentials': 12345}},
    {'cors': {'invalid_subkey': 'value'}},
    {'cache': {'enabled': 'invalid_type'}},
    {'cache': {'time': 'invalid_type'}},
    {'cache': {'invalid_subkey': 'value'}},
    {'access': {'max_login_attempts': 'invalid_type'}},
    {'access': {'block_time': 'invalid_type'}},
    {'access': {'max_request_per_minute': 'invalid_type'}},
    {'access': {'invalid_subkey': 'invalid_type'}},
    {'remote_commands': {'localfile': {'enabled': 'invalid_type'}}},
    {'remote_commands': {'localfile': {'exceptions': [0, 1, 2]}}},
    {'remote_commands': {'localfile': {'invalid_subkey': 'invalid_type'}}},
    {'remote_commands': {'wodle_command': {'enabled': 'invalid_type'}}},
    {'remote_commands': {'wodle_command': {'exceptions': [0, 1, 2]}}},
    {'remote_commands': {'wodle_command': {'invalid_subkey': 'invalid_type'}}},
])
@patch('os.path.exists', return_value=True)
def test_read_wrong_configuration(mock_exists, config):
    """Verify that expected exceptions are raised when incorrect configuration"""
    with patch('api.configuration.yaml.safe_load') as m:
        with pytest.raises(api_exception.APIError, match=r'\b2004\b'):
            configuration.read_yaml_config()

        with patch('builtins.open'):
            m.return_value = config
            with pytest.raises(api_exception.APIError, match=r'\b2000\b'):
                configuration.read_yaml_config()


@patch('os.chmod')
@patch('builtins.open')
def test_generate_private_key(mock_open, mock_chmod):
    """Verify that genetare_private_key returns expected key and 'open' method is called with expected parameters"""
    result_key = configuration.generate_private_key('test_path.crt', 65537, 2048)

    assert result_key.key_size == 2048
    mock_open.assert_called_once_with('test_path.crt', 'wb')
    mock_chmod.assert_called_once()


@patch('os.chmod')
@patch('builtins.open')
def test_generate_self_signed_certificate(mock_open, mock_chmod):
    """Verify that genetare_private_key returns expected key and 'open' method is called with expected parameters"""
    result_key = configuration.generate_private_key('test_path.crt', 65537, 2048)
    configuration.generate_self_signed_certificate(result_key, 'test_path.crt')

    assert mock_open.call_count == 2, 'Not expected number of calls'
    assert mock_chmod.call_count == 2, 'Not expected number of calls'


