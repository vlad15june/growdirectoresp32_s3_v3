Import("env")

# Set environment variable for verbose output
env.Replace(PIOVERBOSE=3)  # Maximum verbosity level

# Add a pre-action to print a message before testing
env.AddPreAction("test", lambda *args, **kwargs: print("Running tests with maximum verbosity..."))

# Define a function to be called after the regular test
def verbose_test_callback(*args, **kwargs):
    # We're using PIOVERBOSE=3, but this makes sure tests run with -vvv
    env.Execute("pio test -vvv")
    return 0
