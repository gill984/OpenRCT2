/*****************************************************************************
 * Copyright (c) 2014-2018 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Plugin.h"

#include <algorithm>
#include <dukglue/dukglue.h>
#include <duktape.h>
#include <fstream>
#include <memory>

#ifdef _WIN32

#else
#    include <fcntl.h>
#    include <sys/inotify.h>
#    include <sys/types.h>
#    include <unistd.h>
#endif

using namespace OpenRCT2::Scripting;

Plugin::Plugin(duk_context* context, const std::string& path)
    : _context(context)
    , _path(path)
{
}

void Plugin::Load()
{
    std::string projectedVariables = "console,context,map,park,ui";
    std::string code;
    {
        std::ifstream fs(_path);
        if (fs.is_open())
        {
            fs.seekg(0, std::ios::end);
            code.reserve(fs.tellg());
            fs.seekg(0, std::ios::beg);
            code.assign(std::istreambuf_iterator<char>(fs), std::istreambuf_iterator<char>());
        }
    }
    // Wrap the script in a function and pass the global objects as variables
    // so that if the script modifies them, they are not modified for other scripts.
    code = "(function(" + projectedVariables + "){" + code + "})(" + projectedVariables + ");";
    auto flags = DUK_COMPILE_EVAL | DUK_COMPILE_SAFE | DUK_COMPILE_NOSOURCE | DUK_COMPILE_NOFILENAME;
    auto result = duk_eval_raw(_context, code.c_str(), code.size(), flags);
    if (result != DUK_ERR_NONE)
    {
        auto val = std::string(duk_safe_to_string(_context, -1));
        duk_pop(_context);
        throw std::runtime_error("Failed to load plug-in script: " + val);
    }

    _metadata = GetMetadata(DukValue::take_from_stack(_context));
}

void Plugin::Start()
{
    const auto& mainFunc = _metadata.Main;
    if (mainFunc.context() == nullptr)
    {
        throw std::runtime_error("No main function specified.");
    }

    mainFunc.push();
    auto result = duk_pcall(_context, 0);
    if (result != DUK_ERR_NONE)
    {
        auto val = std::string(duk_safe_to_string(_context, -1));
        duk_pop(_context);
        throw std::runtime_error("[" + _metadata.Name + "] " + val);
    }
    duk_pop(_context);
}

void Plugin::Update()
{
}

PluginMetadata Plugin::GetMetadata(const DukValue& dukMetadata)
{
    PluginMetadata metadata;
    if (dukMetadata.type() == DukValue::Type::OBJECT)
    {
        metadata.Name = dukMetadata["name"].as_string();
        metadata.Version = dukMetadata["version"].as_string();

        auto dukAuthors = dukMetadata["authors"];
        dukAuthors.push();
        if (dukAuthors.is_array())
        {
            auto elements = dukAuthors.as_array();
            std::transform(elements.begin(), elements.end(), std::back_inserter(metadata.Authors), [](const DukValue& v) {
                return v.as_string();
            });
        }
        else
        {
            metadata.Authors = { dukAuthors.as_string() };
        }
        metadata.Main = dukMetadata["main"];
    }
    return metadata;
}