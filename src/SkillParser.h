#pragma once

#include <JuceHeader.h>

struct SkillFrontmatter
{
    juce::String name;
    juce::String description;
    int stageOrder = 0;
    juce::String nextStage;
};

struct SkillActionBlock
{
    juce::String actionId;
    juce::StringPairArray params;
    juce::String label;
};

struct SkillContentBlock
{
    enum Type { Text, Heading, Action };
    Type type = Text;
    juce::String text;            // for Text and Heading
    int headingLevel = 0;         // for Heading (2 = ##, 3 = ###)
    SkillActionBlock action;      // for Action
};

struct SkillFile
{
    SkillFrontmatter frontmatter;
    juce::Array<SkillContentBlock> blocks;
};

class SkillParser
{
public:
    static SkillFile parse(const juce::String& markdown)
    {
        SkillFile result;
        auto lines = juce::StringArray::fromLines(markdown);

        int i = 0;
        bool inFrontmatter = false;

        // Parse YAML frontmatter
        if (i < lines.size() && lines[i].trimStart() == "---")
        {
            inFrontmatter = true;
            ++i;
            while (i < lines.size())
            {
                auto line = lines[i].trim();
                if (line == "---") { ++i; break; }

                auto colonPos = line.indexOf(":");
                if (colonPos > 0)
                {
                    auto key = line.substring(0, colonPos).trim();
                    auto val = line.substring(colonPos + 1).trim();

                    if (key == "name")         result.frontmatter.name = val;
                    if (key == "description")  result.frontmatter.description = val;
                    if (key == "stage_order")  result.frontmatter.stageOrder = val.getIntValue();
                    if (key == "next_stage")   result.frontmatter.nextStage = val;
                }
                ++i;
            }
        }

        // Parse body content
        while (i < lines.size())
        {
            auto line = lines[i];
            auto trimmed = line.trim();

            // Action block: [ACTION: id | param: value | label: "text"]
            if (trimmed.startsWith("[ACTION:"))
            {
                SkillContentBlock block;
                block.type = SkillContentBlock::Action;
                block.action = parseAction(trimmed);
                result.blocks.add(block);
            }
            // Heading
            else if (trimmed.startsWith("###"))
            {
                SkillContentBlock block;
                block.type = SkillContentBlock::Heading;
                block.headingLevel = 3;
                block.text = trimmed.fromFirstOccurrenceOf("### ", false, false).trim();
                result.blocks.add(block);
            }
            else if (trimmed.startsWith("##"))
            {
                SkillContentBlock block;
                block.type = SkillContentBlock::Heading;
                block.headingLevel = 2;
                block.text = trimmed.fromFirstOccurrenceOf("## ", false, false).trim();
                result.blocks.add(block);
            }
            // Non-empty text line
            else if (trimmed.isNotEmpty())
            {
                // Merge consecutive text lines into one block
                if (result.blocks.size() > 0
                    && result.blocks.getLast().type == SkillContentBlock::Text)
                {
                    result.blocks.getReference(result.blocks.size() - 1).text
                        += "\n" + trimmed;
                }
                else
                {
                    SkillContentBlock block;
                    block.type = SkillContentBlock::Text;
                    block.text = trimmed;
                    result.blocks.add(block);
                }
            }
            // Empty line breaks text merging (next non-empty text starts a new block)

            ++i;
        }

        return result;
    }

private:
    static SkillActionBlock parseAction(const juce::String& line)
    {
        SkillActionBlock action;

        // Strip [ and ]
        auto inner = line.substring(1, line.length() - 1).trim();

        // Split on |
        auto parts = juce::StringArray::fromTokens(inner, "|", "\"");

        for (auto& part : parts)
        {
            auto p = part.trim();

            if (p.startsWith("ACTION:"))
            {
                action.actionId = p.fromFirstOccurrenceOf("ACTION:", false, false).trim();
            }
            else if (p.startsWith("label:"))
            {
                auto val = p.fromFirstOccurrenceOf("label:", false, false).trim();
                action.label = val.unquoted();
            }
            else
            {
                // Generic param: "key: value"
                auto colonPos = p.indexOf(":");
                if (colonPos > 0)
                {
                    auto key = p.substring(0, colonPos).trim();
                    auto val = p.substring(colonPos + 1).trim().unquoted();
                    action.params.set(key, val);
                }
            }
        }

        return action;
    }
};
