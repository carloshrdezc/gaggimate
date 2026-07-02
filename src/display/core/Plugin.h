#ifndef PLUGIN_H
#define PLUGIN_H

class PluginManager;
class Controller;
class Plugin {
  public:
    Plugin() = default;
    virtual ~Plugin() = default;
    // Abstract interface base: deleting copy/move prevents accidental slicing.
    // Derived plugins are heap-managed via Plugin* (see PluginManager), never
    // value-copied. Declaring all five special members satisfies the rule-of-5.
    Plugin(const Plugin &) = delete;
    Plugin &operator=(const Plugin &) = delete;
    Plugin(Plugin &&) = delete;
    Plugin &operator=(Plugin &&) = delete;

    virtual void setup(Controller *controller, PluginManager *pluginManager) = 0;
    virtual void loop() = 0;
};

#endif // PLUGIN_H
