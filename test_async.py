import asyncio
from hpyx._core import hpx_async
import time

# Define a Python function that takes some time
def slow_task():
    print("Started slow task")
    time.sleep(2)  # sleep 2 seconds to simulate work
    print("Finished slow task")
    return 42

async def main():
    # Use hpx_async to run it asynchronously
    start = time.time()
    future = hpx_async(slow_task)
    future2 = hpx_async(slow_task)
    print("Launched async call!")

    # Print status of the event loop
    loop = asyncio.get_running_loop()
    tasks = asyncio.all_tasks(loop)
    print("Current tasks in the event loop:")
    for task in tasks:
        print(" -", task)

    elapsed = time.time() - start
    print(f"Time to launch async: {elapsed:.3f} seconds")
    print("Type of future:", type(future))

    # Properly await the future
    result = await future
    print(f"Result = {result}")
    
    return result

if __name__ == "__main__":
    # Run the async function with asyncio.run
    result = asyncio.run(main())
    print(f"Final result: {result}")