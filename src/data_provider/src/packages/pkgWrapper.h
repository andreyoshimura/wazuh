/*
 * Wazuh SYSINFO
 * Copyright (C) 2015-2021, Wazuh Inc.
 * December 14, 2020.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#ifndef _PKG_WRAPPER_H
#define _PKG_WRAPPER_H

#include <fstream>
#include <istream>
#include "stringHelper.h"
#include "ipackageWrapper.h"
#include "sharedDefs.h"
#include "plist/plist.h"

static const std::string APP_INFO_PATH      { "Contents/Info.plist" };
static const std::string PLIST_BINARY_START { "bplist00"            };
static const std::string UTILITIES_FOLDER   { "/Utilities"          };

class PKGWrapper final : public IPackageWrapper
{
public:
    explicit PKGWrapper(const PackageContext& ctx)
      : m_format{"pkg"}
    {
        getPkgData(ctx.filePath+ "/" + ctx.package + "/" + APP_INFO_PATH);
    }

    ~PKGWrapper() = default;

    std::string name() const override
    {
        return m_name;
    }
    std::string version() const override
    {
        return m_version;
    }
    std::string groups() const override
    {
        return m_groups;
    }
    std::string description() const override
    {
        return m_description;
    }
    std::string architecture() const override
    {
        return m_architecture;
    }
    std::string format() const override
    {
        return m_format;
    }
    std::string osPatch() const override
    {
        return m_osPatch;
    }
    std::string source() const override
    {
        return m_source;
    }
    std::string location() const override
    {
        return m_location;
    }

private:
    void getPkgData(const std::string& filePath)
    {
        const auto isBinaryFnc
        {
            [&filePath]()
            {
                // If first line is "bplist00" it's a binary plist file
                std::fstream file {filePath, std::ios_base::in};
                std::string line;
                return std::getline(file, line) && Utils::startsWith(line, PLIST_BINARY_START);
            }
        };
        const auto isBinary { isBinaryFnc() };

        static const auto getValueFnc
        {
            [](const std::string& val)
            {
                const auto start{val.find(">")};
                const auto end{val.rfind("<")};
                return val.substr(start+1, end - start -1);
            }
        };

        const auto getDataFnc
        {
            [this, &filePath](std::istream& data)
            {
                std::string line;
                while(std::getline(data, line))
                {
                    line = Utils::trim(line," \t");

                    if (line == "<key>CFBundleName</key>" &&
                        std::getline(data, line))
                    {
                        m_name = getValueFnc(line);
                    }
                    else if (line == "<key>CFBundleShortVersionString</key>" &&
                            std::getline(data, line))
                    {
                        m_version = getValueFnc(line);
                    }
                    else if (line == "<key>LSApplicationCategoryType</key>" &&
                            std::getline(data, line))
                    {
                        m_groups = getValueFnc(line);
                    }
                    else if (line == "<key>CFBundleIdentifier</key>" &&
                            std::getline(data, line))
                    {
                        m_description = getValueFnc(line);
                    }
                }
                m_source   = filePath.find(UTILITIES_FOLDER) ? "utilities" : "applications";
                m_location = filePath;
            }
        };

        if (isBinary)
        {
            auto xmlContent { binaryToXML(filePath) };
            getDataFnc(xmlContent);
        }
        else
        {
            std::fstream file { filePath, std::ios_base::in };
            if (file.is_open())
            {
                getDataFnc(file);
            }
        }
    }

    std::stringstream binaryToXML(const std::string& filePath)
    {
        std::string xmlContent;
        plist_t rootNode { nullptr };
        const auto binaryContent { Utils::getBinaryContent(filePath) };

        // plist C++ APIs calls - to be used when Makefile and external are updated.
        // const auto dataFromBin { PList::Structure::FromBin(binaryContent) };
        // const auto xmlContent { dataFromBin->ToXml() };

        // Content binary file to plist representation
        plist_from_bin(binaryContent.data(), binaryContent.size(), &rootNode);
        if (nullptr != rootNode)
        {
            char* xml { nullptr };
            uint32_t length { 0 };
            // plist binary representation to XML
            plist_to_xml(rootNode, &xml, &length);
            if (nullptr != xml)
            {
                xmlContent.assign(xml, xml+length);
                plist_to_xml_free(xml);
                plist_free(rootNode);
            }
        }
        return std::stringstream{xmlContent};
    }

    std::string m_name;
    std::string m_version;
    std::string m_groups;
    std::string m_description;
    std::string m_architecture;
    const std::string m_format;
    std::string m_osPatch;
    std::string m_source;
    std::string m_location;
};

#endif //_PKG_WRAPPER_H
