#ifndef OpenAIVisionProxy_h
#define OpenAIVisionProxy_h

#include <Arduino.h>

// Placeholder extension point to decouple camera provider integrations.
class OpenAIVisionProxy {
  public:
    static String BuildContextPrompt(const String& basePrompt, const String& visualContext) {
      if (visualContext.length() == 0) {
        return basePrompt;
      }
      String prompt = basePrompt;
      prompt += "\n\n[Current visual context: " + visualContext + "]";
      prompt += "\nReference this naturally when relevant.";
      return prompt;
    }
};

#endif
