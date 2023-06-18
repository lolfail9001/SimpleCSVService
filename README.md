# Description
A very basic CSV manipulation service. Current version supports: uploading CSVs with basic type inference, viewing the list of uploaded CSVs and viewing uploaded CSVs with basic filtering and sorting capabilities.

# Setup and usage
Hard requirements are SQLite3 and building environment with C++17 support.
``` shell
cmake .
cmake --build .
```

Run this with
``` shell
./csvapp -p Port -db DBFilename
```
to run the HTTP service listening on IPAddr:Port using an SQLite3 db DBFilename. Defaults are ```127.0.0.1``` for ```IPAddr```, ```8000``` for ```Port``` and ```csvs.db``` for ```DBFilename```. It is expected to use the described 3 parameters only once each.
# API description
Current version supports following requests:
```upload``` -- Requires a request with 2 form data entries named ```csv_name``` and ```csv_file```. Former is additionally required to contain ASCII string (in current version behavior is undefined otherwise). Creates a DB table using content of ```csv_name``` and fills it with contents of ```csv_file``` (required to be a proper CSV file, desirably with every entry of second row being of proper type). Types of columns are inferred from the uploaded CSV file, albeit only with  support int, float and ascii string.

```tables``` -- Generates a JSON containing names (as stored in DB) of uploaded CSV files as well as their column names if present in original CSV (otherwise filler column names 1,2,... are generated). Table descriptions lie in JSON entry ```tables```

```view``` -- using GET parameters ```name,col,asc,desc,from,to``` requests rows from ```from``` (defaults to 0) to ```to``` (defaults to 100) in table under name ```name```, and displays columns indexed by ```col``` ordered by columns indexed by ```asc``` in ascending order and columns indexed by ```desc``` in descending order. The final view is packed into a JSON as entry ```contents```.

Additionally, every status 200 response generated contains a JSON entry ```issues``` that describe issues that arose from request such as using non-integer indexes et cetera.


NOT TESTED PROPERLY: ```update``` -- takes params ```name``` and ```rowid``` as well as JSON form data ```row_desc``` to update an existing row ```rowid``` (or insert a new row) of table ```name```. ```row_desc``` is required to be of format ```"column name":"column data"``` with appropriate screened characters.

NOT IMPLEMENTED PROPERLY: A basic SQL injection protection in ```update``` and ```upload``` calls, proper backpropagation of errors (requires monadization of code).


# Acknoledgements
To [cpp-httplib library](https://github.com/yhirose/cpp-httplib) and it's creator for wrapping up the ridiculous boiler plate that is HTTP in C++ into nice C++11 library.
