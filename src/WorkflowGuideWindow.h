#pragma once

#include <JuceHeader.h>
#include "SkillParser.h"
#include "DawTheme.h"

class WorkflowGuideWindow : public juce::DocumentWindow
{
public:
    // Callback type for action buttons
    using ActionCallback = std::function<void(const juce::String& actionId,
                                              const juce::StringPairArray& params)>;

    WorkflowGuideWindow(ActionCallback onAction)
        : DocumentWindow("Workflow Guide",
                         juce::Colours::darkgrey,
                         DocumentWindow::closeButton),
          actionCallback(std::move(onAction))
    {
        setUsingNativeTitleBar(false);
        setResizable(true, true);
        setSize(340, 500);

        content = std::make_unique<ContentComponent>(*this);
        setContentNonOwned(content.get(), false);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

    void loadSkillFromString(const juce::String& markdown)
    {
        currentSkill = SkillParser::parse(markdown);
        if (content) content->rebuild(currentSkill);
    }

    void setStageIndex(int index)
    {
        if (content) content->setStageDropdown(index);
    }

    // Called when the theme changes
    void applyTheme(const DawTheme& theme)
    {
        currentTheme = theme;
        setBackgroundColour(juce::Colour(theme.body));
        if (content) content->applyTheme(theme);
    }

    // Stage change callback — MainComponent sets this to load the right file
    std::function<void(int stageIndex)> onStageChanged;

    // Persistence
    juce::String getStateAsString() const
    {
        auto bounds = getBounds();
        juce::String state;
        state << bounds.getX() << "," << bounds.getY() << ","
              << bounds.getWidth() << "," << bounds.getHeight() << ","
              << (isVisible() ? 1 : 0);
        if (content) state << "," << content->getSelectedStage();
        return state;
    }

    void restoreFromString(const juce::String& state)
    {
        auto tokens = juce::StringArray::fromTokens(state, ",", "");
        if (tokens.size() >= 5)
        {
            setBounds(tokens[0].getIntValue(), tokens[1].getIntValue(),
                      tokens[2].getIntValue(), tokens[3].getIntValue());
            if (tokens[3].getIntValue() > 0)
                setVisible(tokens[4].getIntValue() == 1);
            if (tokens.size() >= 6 && content)
                content->setStageDropdown(tokens[5].getIntValue());
        }
    }

private:
    ActionCallback actionCallback;
    SkillFile currentSkill;
    DawTheme currentTheme {};

    class ContentComponent : public juce::Component
    {
    public:
        ContentComponent(WorkflowGuideWindow& owner) : owner(owner)
        {
            addAndMakeVisible(stageSelector);
            stageSelector.addItem("1. Seed", 1);
            stageSelector.addItem("2. Layer", 2);
            stageSelector.addItem("3. Shape", 3);
            stageSelector.addItem("4. Extend", 4);
            stageSelector.setSelectedId(1, juce::dontSendNotification);
            stageSelector.onChange = [this]()
            {
                if (this->owner.onStageChanged)
                    this->owner.onStageChanged(stageSelector.getSelectedId() - 1);
            };

            addAndMakeVisible(viewport);
            viewport.setViewedComponent(&scrollContent, false);
            viewport.setScrollBarsShown(true, false);
        }

        void setStageDropdown(int index)
        {
            stageSelector.setSelectedId(index + 1, juce::dontSendNotification);
        }

        int getSelectedStage() const
        {
            return stageSelector.getSelectedId() - 1;
        }

        void rebuild(const SkillFile& skill)
        {
            scrollContent.clearContent();

            for (auto& block : skill.blocks)
            {
                if (block.type == SkillContentBlock::Heading)
                    scrollContent.addHeading(block.text, block.headingLevel, owner.currentTheme);
                else if (block.type == SkillContentBlock::Text)
                    scrollContent.addText(block.text, owner.currentTheme);
                else if (block.type == SkillContentBlock::Action)
                    scrollContent.addAction(block.action, owner.currentTheme, owner.actionCallback);
            }

            scrollContent.layoutContent(viewport.getWidth() - 16);
            viewport.setViewPosition(0, 0);
        }

        void applyTheme(const DawTheme& theme)
        {
            repaint();
        }

        void resized() override
        {
            auto area = getLocalBounds();
            stageSelector.setBounds(area.removeFromTop(36).reduced(8, 4));
            viewport.setBounds(area.reduced(4));
            scrollContent.layoutContent(viewport.getWidth() - 16);
        }

    private:
        WorkflowGuideWindow& owner;
        juce::ComboBox stageSelector;
        juce::Viewport viewport;

        // Scrollable content area that holds all the rendered blocks
        class ScrollContent : public juce::Component
        {
        public:
            void clearContent()
            {
                labels.clear();
                buttons.clear();
                removeAllChildren();
            }

            void addHeading(const juce::String& text, int level, const DawTheme& theme)
            {
                auto* label = labels.add(std::make_unique<juce::Label>());
                float fontSize = (level <= 2) ? 16.0f : 13.0f;
                label->setFont(juce::Font("Consolas", fontSize, juce::Font::bold));
                label->setText(text, juce::dontSendNotification);
                label->setColour(juce::Label::textColourId, juce::Colour(theme.textBright));
                label->setJustificationType(juce::Justification::topLeft);
                addAndMakeVisible(label);
            }

            void addText(const juce::String& text, const DawTheme& theme)
            {
                auto* label = labels.add(std::make_unique<juce::Label>());
                label->setFont(juce::Font("Consolas", 12.0f, juce::Font::plain));
                label->setText(text, juce::dontSendNotification);
                label->setColour(juce::Label::textColourId, juce::Colour(theme.textPrimary));
                label->setJustificationType(juce::Justification::topLeft);
                label->setMinimumHorizontalScale(1.0f);
                addAndMakeVisible(label);
            }

            void addAction(const SkillActionBlock& action, const DawTheme& theme,
                           const std::function<void(const juce::String&,
                                                     const juce::StringPairArray&)>& callback)
            {
                auto* btn = buttons.add(std::make_unique<juce::TextButton>(action.label));
                btn->setColour(juce::TextButton::buttonColourId, juce::Colour(theme.green));
                btn->setColour(juce::TextButton::textColourOffId, juce::Colour(theme.textBright));

                auto id = action.actionId;
                auto params = action.params;
                btn->onClick = [callback, id, params]
                {
                    if (callback) callback(id, params);
                };
                addAndMakeVisible(btn);
            }

            void layoutContent(int width)
            {
                int y = 8;
                int pad = 6;

                for (int i = 0; i < getNumChildComponents(); ++i)
                {
                    auto* child = getChildComponent(i);

                    if (auto* label = dynamic_cast<juce::Label*>(child))
                    {
                        auto font = label->getFont();
                        int textHeight = juce::jmax(20, (int)(font.getHeight() * 1.4f)
                            * juce::jmax(1, (int)std::ceil(
                                font.getStringWidthFloat(label->getText()) / (float)width)));
                        // Add extra height for multi-line text
                        int lineCount = 1;
                        for (int c = 0; c < label->getText().length(); ++c)
                            if (label->getText()[c] == '\n') ++lineCount;
                        textHeight = juce::jmax(textHeight, lineCount * (int)(font.getHeight() * 1.4f));

                        label->setBounds(8, y, width - 16, textHeight);
                        y += textHeight + pad;
                    }
                    else if (auto* btn = dynamic_cast<juce::TextButton*>(child))
                    {
                        btn->setBounds(8, y, width - 16, 32);
                        y += 32 + pad;
                    }
                }

                setSize(width, y + 8);
            }

        private:
            juce::OwnedArray<juce::Label> labels;
            juce::OwnedArray<juce::TextButton> buttons;
        };

        ScrollContent scrollContent;
    };

    std::unique_ptr<ContentComponent> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkflowGuideWindow)
};
